.DEFAULT_GOAL := all
SHARED := $(abspath ../bl616_usb_ncm)
OUT ?= $(CURDIR)/build
.PHONY: all whole clean
all whole clean:
	$(MAKE) -C $(SHARED) $@ CHIP=bl618dg BOARD=bl618dgdk OUT=$(OUT)
