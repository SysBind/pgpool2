#!/usr/bin/env bash

if [ "$1" = 'pgpool' ]; then
    if [ -f /usr/local/etc/pgpool.conf ]; then
	cp -v /usr/local/etc/pgpool.conf /etc/pgpool.conf
    else
	cp -v /usr/local/etc/pgpool.conf.sample /etc/pgpool.conf
    fi
    # prevents error binding to ::1 inside container (address not available)
    echo "listen_addresses = '*'" >> /etc/pgpool.conf

    PGPOOL_PORT=${PGPOOL_PORT:-9999}
    echo "port = ${PGPOOL_PORT}" >> /etc/pgpool.conf
    
    if [ [ ! -z ${PGPOOL_WD_HOST_COUNT+x}] && [ ! ${PGPOOL_WD_HOST_COUNT} = "0" ] ]; then
	PGPOOL_WD_PORT=${PGPOOL_WD_PORT:-9000}
	PGPOOL_DOMAIN_SUFFIX=${PGPOOL_DOMAIN_SUFFIX:-""}
	
	echo "use_watchdog = on" >> /etc/pgpool.conf
	echo "wd_hostname = '"${HOSTNAME}${PGPOOL_DOMAIN_SUFFIX}"'" >> /etc/pgpool.conf
	echo "wd_port = ${PGPOOL_WD_PORT}" >> /etc/pgpool.conf
	echo "wd_heartbeat_port = 9694" >> /etc/pgpool.conf
	echo "wd_lifecheck_method = 'heartbeat'" >> /etc/pgpool.conf
	echo "wd_authkey = ''" >> /etc/pgpool.conf
	idx=0
	base_hostname=`echo $HOSTNAME | rev | cut -f2- -d- | rev`
	for ((i=0; i<${PGPOOL_WD_HOST_COUNT}; i++))
	do
	    # skip adding my self
	    [ "$base_hostname-$i" = "$HOSTNAME" ] && continue

	    # add other_pgpool_hostname
	    echo "other_pgpool_hostname${idx} = '$base_hostname-$i${PGPOOL_DOMAIN_SUFFIX}'" >> /etc/pgpool.conf
	    echo "other_pgpool_port${idx} = ${PGPOOL_PORT}" >> /etc/pgpool.conf
	    echo "other_wd_port${idx} = ${PGPOOL_WD_PORT}" >> /etc/pgpool.conf
	    echo "heartbeat_destination${idx} = '$base_hostname-$i${PGPOOL_DOMAIN_SUFFIX}'" >> /etc/pgpool.conf
	    echo "heartbeat_destination_port${idx} = 9694" >> /etc/pgpool.conf
	    idx=$((idx+1))	
	done	
    fi

    echo "executing $@ -f /etc/pgpool.conf"
    exec "$@" -f /etc/pgpool.conf
fi

echo "executing $@"
exec "$@"
