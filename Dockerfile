FROM postgres:11-alpine

MAINTAINER Asaf Ohayon <asaf@sysbind.co.il>


RUN set -ex \
	\
    && apk add --no-cache --virtual .build-deps \
       gcc \
       libc-dev \
       linux-headers \
       make


COPY . /usr/src/pgpool-II

RUN set -ex \
    \
    && cd /usr/src/pgpool-II \
    && ./configure \
    && make clean \
    && make \
    && make install \
    && apk del .build-deps \
    && rm -rf /usr/src/pgpool-II

RUN mkdir -p /var/run/pgpool && chown -R postgres:postgres /var/run/pgpool && chmod 2777 /var/run/pgpool

COPY dockerfiles/docker-entrypoint.sh /usr/local/bin/

ENTRYPOINT ["docker-entrypoint.sh"]

EXPOSE 5432
# heartbeat
EXPOSE 9694
# pcp
EXPOSE 9898
# watchdog
EXPOSE 9000

CMD ["pgpool","-n"]
