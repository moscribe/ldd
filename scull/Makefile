#DEBUG=y

ifeq ($(DEBUG), y)
  DEBFLAGS = -O -g -DSCULL_DEBUG
else
  DEBFLAGS = -O2
endif

CFLAGS += $(DEBFLAGS)

ifneq ($(KERNELRELEASE), )
#call from kernel build system

scull-objs := main.o

obj-m := scull.o

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD)
endif

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

depend .depend dep:
	$(CC) $(CFLAGS) -M *.c > .depend

ifeq (.depend, $(wildcard .depent))
include .depend
endif