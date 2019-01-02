FROM postgres:11-alpine

MAINTAINER Asaf Ohayon <asaf@sysbind.co.il>


COPY . /usr/src/pgpool-II

RUN set -ex \
	\
    && apk add --no-cache --virtual .build-deps \
       gcc \
       libc-dev \
       linux-headers \
       make


RUN set -ex \
    \
    && cd /usr/src/pgpool-II \
    && patch -p1 < dockerfiles/fix_compile_alpine38.patch \
    && ./configure \
    && make clean \
    && make \
    && make install \
    && apk del .build-deps \
    && rm -rf /usr/src/pgpool-II

RUN mkdir -p /var/run/pgpool && chown -R postgres:postgres /var/run/pgpool && chmod 2777 /var/run/pgpool

ENTRYPOINT ["docker-entrypoint.sh"]

EXPOSE 5432
# heartbeat
EXPOSE 9694
# pcp
EXPOSE 9898
# watchdog
EXPOSE 9000

CMD ["pgpool","-n"]
