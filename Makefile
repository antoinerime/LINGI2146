CONTIKI = ../contiki

all: sensor

CONTIKI_WITH_RIME = 1
include $(CONTIKI)/Makefile.include
