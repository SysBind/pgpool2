/*
 * ping.h
 *
 * ICMP Pinging support
 *
 * Portions Copyright (c) 2001-2019, PostgreSQL Global Development Group
 *
 * src/include/utils/ping.h
 */
#ifndef PING_H
#define PING_H

bool ping(char *target_host);

#endif							/* PING_H */
