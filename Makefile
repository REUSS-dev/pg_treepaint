MODULES = pg_treepaint
PG_CONFIG = pg_config #/usr/local/pgsql/bin/pg_config
EXTENSION = pg_treepaint

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)