CONTIKI = ../contiki

all: # We don't use make all ...

CONTIKI_WITH_RIME = 1
include $(CONTIKI)/Makefile.include
