# environment variables

## PGPOOL_WD_HOST_COUNT 
- type: integer
- default: none

when defined, will configure pgpool to connect to watchdog cluster,
assuming hostname are ordinal, starting from zero. e.g: pgpool-0, pgpool-1
(such as in k8s stateful sets, can be also used in normal docker)

## PGPOOL_WD_PORT : integer (port number)
- type: integer
- default: 9000
