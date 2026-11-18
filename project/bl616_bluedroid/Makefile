.DEFAULT_GOAL := all
SHARED := $(abspath ../bl618dg_pan)
OUT ?= $(CURDIR)/build
.PHONY: all whole clean
all whole clean:
	$(MAKE) -C $(SHARED) $@ CHIP=bl616 BOARD=bl616dk OUT=$(OUT) $(if $(SPP),SPP=$(SPP)) $(if $(WIFI_ONLY),WIFI_ONLY=$(WIFI_ONLY))
