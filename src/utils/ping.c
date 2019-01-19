/*
Copyright (c) 2018 Sergey Zolotarev

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>           /* fcntl() */
#include <netdb.h>           /* getaddrinfo() */
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>       /* inet_XtoY() */
#include <netinet/in.h>      /* IPPROTO_ICMP */
#include <netinet/ip.h>
#include <netinet/ip_icmp.h> /* struct icmp */
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include "pool.h"
#include "utils/ping.h"
#include "utils/elog.h"

#define IP_VERISON_ANY 0
#define IP_V4 4
#define IP_V6 6

#define MIN_IP_HEADER_SIZE 20
#define MAX_IP_HEADER_SIZE 60
#define MAX_IP6_PSEUDO_HEADER_SIZE 40

#ifndef ICMP_ECHO
    #define ICMP_ECHO 8
#endif
#ifndef ICMP_ECHO6
    #define ICMP6_ECHO 128
#endif
#ifndef ICMP_ECHO_REPLY
    #define ICMP_ECHO_REPLY 0
#endif
#ifndef ICMP_ECHO_REPLY6
    #define ICMP6_ECHO_REPLY 129
#endif

#define REQUEST_TIMEOUT 1000000
#define REQUEST_INTERVAL 1000000

#pragma pack(push, 1)

struct ip6_pseudo_hdr {
    struct in6_addr ip6_src;
    struct in6_addr ip6_dst;
    uint32_t ip6_plen;
    uint8_t ip6_zero[3];
    uint8_t ip6_nxt;
};

#pragma pack(pop)

/*
 * RFC 1071 - http://tools.ietf.org/html/rfc1071
 */
static uint16_t compute_checksum(const char *buf, size_t size) {
    size_t i;
    uint64_t sum = 0;

    for (i = 0; i < size; i += 2) {
        sum += *(uint16_t *)buf;
        buf += 2;
    }
    if (size - i > 0) {
        sum += *(uint8_t *)buf;
    }

    while ((sum >> 16) != 0) {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

static uint64_t get_time(void) {
    struct timeval now;
    return gettimeofday(&now, NULL) != 0
        ? 0
        : now.tv_sec * 1000000 + now.tv_usec;
}

bool ping(char *target_host) {
    int ip_version = IP_VERISON_ANY;
    int gai_error;
    int sockfd = -1;
    struct addrinfo addrinfo_hints;
    struct addrinfo *addrinfo_head;
    struct addrinfo *addrinfo_ptr;
    struct addrinfo addrinfo;
    void *addr = 0;
    char addrstr[INET6_ADDRSTRLEN] = "<unknown>";
    uint16_t id = (uint16_t)getpid();
    uint16_t seq;

    if (target_host == NULL) {
        ereport(WARNING, (errmsg("ping: NULL Passed as target_host")));
        return false;
    }
    ereport(DEBUG1,	(errmsg("ping %s", target_host)));

    if (ip_version == IP_V4 || ip_version == IP_VERISON_ANY) {
        memset(&addrinfo_hints, 0, sizeof(addrinfo_hints));
        addrinfo_hints.ai_family = AF_INET;
        addrinfo_hints.ai_socktype = SOCK_RAW;
        addrinfo_hints.ai_protocol = IPPROTO_ICMP;
        gai_error = getaddrinfo(target_host,
                                NULL,
                                &addrinfo_hints,
                                &addrinfo_head);
    }

    if (ip_version == IP_V6
        || (ip_version == IP_VERISON_ANY && gai_error != 0)) {
        memset(&addrinfo_hints, 0, sizeof(addrinfo_hints));
        addrinfo_hints.ai_family = AF_INET6;
        addrinfo_hints.ai_socktype = SOCK_RAW;
        addrinfo_hints.ai_protocol = IPPROTO_ICMPV6;
        gai_error = getaddrinfo(target_host,
                                NULL,
                                &addrinfo_hints,
                                &addrinfo_head);
    }

    if (gai_error != 0) {
        ereport(WARNING, (errmsg("getaddrinfo: %s", gai_strerror(gai_error))));
        return false;
    }

    for (addrinfo_ptr = addrinfo_head;
         addrinfo_ptr != NULL;
         addrinfo_ptr = addrinfo_ptr->ai_next) {
        sockfd = socket(addrinfo_ptr->ai_family,
                        addrinfo_ptr->ai_socktype,
                        addrinfo_ptr->ai_protocol);
        if (sockfd >= 0) {
            break;
        }
    }

    if (sockfd < 0) {
        ereport(WARNING, (errmsg("socket: %s",  strerror(errno))));
        freeaddrinfo(addrinfo_head);
        return false;
    }

    memcpy(&addrinfo, addrinfo_ptr, sizeof(struct addrinfo));
    addrinfo.ai_next = NULL;
    freeaddrinfo(addrinfo_ptr);

    switch (addrinfo.ai_family) {
        case AF_INET:
            addr = &((struct sockaddr_in *)addrinfo.ai_addr)->sin_addr;
            break;
        case AF_INET6:
            addr = &((struct sockaddr_in6 *)addrinfo.ai_addr)->sin6_addr;
            break;
    }

    inet_ntop(addrinfo.ai_family,
              addr,
              addrstr,
              sizeof(addrstr));

    if (fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1) {
        ereport(WARNING, (errmsg("fcntl %s", strerror(errno))));
        return false;
    }

    for (seq = 0; seq < 5; seq++) {
        struct icmp icmp_request = {0};
        int send_result;
        char recv_buf[MAX_IP_HEADER_SIZE + sizeof(struct icmp)];
        int recv_size;
        int recv_result;
        socklen_t addrlen;
        uint8_t ip_vhl;
        uint8_t ip_header_size;
        struct icmp *icmp_response;
        uint64_t start_time;
        uint64_t delay;
        uint16_t checksum;
        uint16_t expected_checksum;

        if (seq > 0) {
             usleep(REQUEST_INTERVAL);
        }

        icmp_request.icmp_type =
            addrinfo.ai_family == AF_INET6 ? ICMP6_ECHO : ICMP_ECHO;
        icmp_request.icmp_code = 0;
        icmp_request.icmp_cksum = 0;
        icmp_request.icmp_id = htons(id);
        icmp_request.icmp_seq = htons(seq);

        switch (addrinfo.ai_family) {
            case AF_INET:
                icmp_request.icmp_cksum =
                    compute_checksum((const char *)&icmp_request,
                                     sizeof(icmp_request));
                break;
            case AF_INET6: {
                /*
                 * Checksum is calculated from the ICMPv6 packet prepended
                 * with an IPv6 "pseudo-header" (this is different from IPv4).
                 *
                 * https://tools.ietf.org/html/rfc2463#section-2.3
                 * https://tools.ietf.org/html/rfc2460#section-8.1
                 */
                struct {
                    struct ip6_pseudo_hdr ip6_hdr;
                    struct icmp icmp;
                } data = {0};

                data.ip6_hdr.ip6_src.s6_addr[15] = 1; /* ::1 (loopback) */
                data.ip6_hdr.ip6_dst =
                    ((struct sockaddr_in6 *)&addrinfo.ai_addr)->sin6_addr;
                data.ip6_hdr.ip6_plen = htonl((uint32_t)sizeof(struct icmp));
                data.ip6_hdr.ip6_nxt = IPPROTO_ICMPV6;
                data.icmp = icmp_request;

                icmp_request.icmp_cksum =
                    compute_checksum((const char *)&data, sizeof(data));
                break;
            }
        }

        send_result = sendto(sockfd,
                             (const char *)&icmp_request,
                             sizeof(icmp_request),
                             0,
                             addrinfo.ai_addr,
                             addrinfo.ai_addrlen);
        if (send_result < 0) {
            ereport(WARNING, (errmsg("sendto: %s",  strerror(errno))));
            return false;
        }

        ereport(DEBUG1, (errmsg("Sent ICMP echo request to %s", addrstr)));

        switch (addrinfo.ai_family) {
            case AF_INET:
                recv_size = (int)(MAX_IP_HEADER_SIZE + sizeof(struct icmp));
                break;
            case AF_INET6:
                /* When using IPv6 we don't receive IP headers in recvfrom. */
                recv_size = (int)sizeof(struct icmp);
                break;
        }

        start_time = get_time();

        for (int j = 1; j < 2; j++) {
            delay = get_time() - start_time;

            addrlen = addrinfo.ai_addrlen;
            recv_result = recvfrom(sockfd,
                                   recv_buf,
                                   recv_size,
                                   0,
                                   addrinfo.ai_addr,
                                   &addrlen);
            if (recv_result == 0) {
                ereport(DEBUG1,  (errmsg("ping: connection closed")));
                break;
            }
            if (recv_result < 0) {
                if (errno == EAGAIN) {
                    if (delay > REQUEST_TIMEOUT) {
                        ereport(WARNING, (errmsg("ping: Request timed out")));
                        break;
                    } else {
                        /* No data available yet, try to receive again. */
                        continue;
                    }
                } else {
                    ereport(WARNING, (errmsg("recvfrom: %s", strerror(errno))));
                    break;
                }
            }

            switch (addrinfo.ai_family) {
                case AF_INET:
                    /* In contrast to IPv6, for IPv4 connections we do receive
                     * IP headers in incoming datagrams.
                     *
                     * VHL = version (4 bits) + header length (lower 4 bits).
                     */
                    ip_vhl = *(uint8_t *)recv_buf;
                    ip_header_size = (ip_vhl & 0x0F) * 4;
                    break;
                case AF_INET6:
                    ip_header_size = 0;
                    break;
            }

            icmp_response = (struct icmp *)(recv_buf + ip_header_size);
            icmp_response->icmp_cksum = ntohs(icmp_response->icmp_cksum);
            icmp_response->icmp_id = ntohs(icmp_response->icmp_id);
            icmp_response->icmp_seq = ntohs(icmp_response->icmp_seq);

            if (icmp_response->icmp_id == id
                && ((addrinfo.ai_family == AF_INET
                        && icmp_response->icmp_type == ICMP_ECHO_REPLY)
                    ||
                    (addrinfo.ai_family == AF_INET6
                        && (icmp_response->icmp_type != ICMP6_ECHO
                            || icmp_response->icmp_type != ICMP6_ECHO_REPLY))
                )
            ) {
                break;
            }
        }

        if (recv_result <= 0) {
            continue;
        }

        checksum = icmp_response->icmp_cksum;
        icmp_response->icmp_cksum = 0;

        switch (addrinfo.ai_family) {
            case AF_INET:
                expected_checksum =
                    compute_checksum((const char *)icmp_response,
                                     sizeof(*icmp_response));
                break;
            case AF_INET6: {
                struct {
                    struct ip6_pseudo_hdr ip6_hdr;
                    struct icmp icmp;
                } data = {0};

                /* TODO: Need to get sourcea nd destination address somehow... */
                /* data.ip6_hdr.ip6_src = ... */
                /* data.ip6_hdr.ip6_dst = ... */
                data.ip6_hdr.ip6_plen = htonl((uint32_t)sizeof(struct icmp));
                data.ip6_hdr.ip6_nxt = IPPROTO_ICMPV6;
                data.icmp = *icmp_response;

                expected_checksum =
                    compute_checksum((const char *)&data, sizeof(data));
                break;
            }
        }

        ereport(DEBUG1, (errmsg("Received ICMP echo reply from %s: seq=%d, time=%.3f ms",
                addrstr,
                icmp_response->icmp_seq,
                delay / 1000.0)));

        if (checksum != expected_checksum) {
            ereport(DEBUG1, (errmsg("incorrect checksum: %x != %x",
                    checksum,
                    expected_checksum)));
        } 
        return true;
    }
    return false;
}
