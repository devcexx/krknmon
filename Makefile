VERSION		:= 0.1
DKMS_INS_PATH	:= /usr/src/krknmon-$(VERSION)
TARGET          := $(shell uname -r)
obj-m 		+= krknmon.o

build:
	make -C /lib/modules/$(TARGET)/build M=$(shell pwd) modules
clean:
	make -C /lib/modules/$(TARGET)/build M=$(shell pwd) clean

dkms-install:
	mkdir $(DKMS_INS_PATH)
	cp dkms.conf krknmon.c Makefile $(DKMS_INS_PATH)
	dkms add krknmon/$(VERSION)
	dkms build krknmon/$(VERSION)
	dkms install krknmon/$(VERSION)

install: dkms-install

dkms-remove:
	dkms remove krknmon/$(VERSION) --all
	rm -rf $(DKMS_INS_PATH)
	
uninstall: dkms-remove
