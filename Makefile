CONTIKI = ../contiki

all: sensor root utils

CONTIKI_WITH_RIME = 1
include $(CONTIKI)/Makefile.include
