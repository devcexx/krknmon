VERSION		:= 0.3
DKMS_INS_PATH	:= $(ROOTDIR)/usr/src/krknmon-$(VERSION)
TARGET          := $(shell uname -r)
obj-m 		+= krknmon.o

build:
	make -C /lib/modules/$(TARGET)/build M=$(shell pwd) modules
clean:
	make -C /lib/modules/$(TARGET)/build M=$(shell pwd) clean

install:
	mkdir -p $(DKMS_INS_PATH)
	cp dkms.conf krknmon.c Makefile $(DKMS_INS_PATH)

dkms-install: install
	dkms add krknmon/$(VERSION)
	dkms build krknmon/$(VERSION)
	dkms install krknmon/$(VERSION)

dkms-remove:
	dkms remove krknmon/$(VERSION) --all

uninstall: dkms-remove
	rm -rf $(DKMS_INS_PATH)
