#!/bin/sh

ifneq ($(KERNELRELEASE),)
obj-m := usb_v1.o

else
KDIR ?= /lib/modules/`uname -r`/build

default:
	$(MAKE) -C $(KDIR) M=$$PWD
endif


