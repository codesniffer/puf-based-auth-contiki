CONTIKI = ../../..
APPS = powertrace collect-view
#CONTIKI_PROJECT = udp-sender udp-sink
CONTIKI_PROJECT = cert-service-client cert-service-provider
PROJECT_SOURCEFILES += collect-common.c

ifdef PERIOD
CFLAGS=-DPERIOD=$(PERIOD)
endif

all: $(CONTIKI_PROJECT)

CONTIKI_WITH_IPV6 = 1
include $(CONTIKI)/Makefile.include
