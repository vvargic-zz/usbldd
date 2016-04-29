#!/bin/sh

obj-m := usb_v1.o

PWD = $(shell pwd)
KDIR ?= /lib/modules/`uname -r`/build

all:
	$(MAKE) -C $(KDIR) M=$$PWD
clean:
	rm -rf *.o *.ko *.mod.* *.symvers *.order *~


