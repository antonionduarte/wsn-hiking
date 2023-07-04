ifeq ($(NODE_SELECT), end) 
	CONTIKI_PROJECT = end-node
	PROJECT_SOURCEFILES += end-node.c
endif

ifeq ($(NODE_SELECT), relay)
	CONTIKI_PROJECT = relay-node
	PROJECT_SOURCEFILES += relay-node.c
endif

all: $(CONTIKI_PROJECT)

MODULES += $(CONTIKI)/os/services/shell
CONTIKI = ${CNG_PATH}
PROJECTDIRS += $(CURDIR)/src

include $(CONTIKI)/Makefile.include

