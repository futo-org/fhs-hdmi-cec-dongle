# SPDX-License-Identifier: GPL-2.0 OR MIT
# Out-of-tree build. Kbuild reads ./Kbuild, not this file.

KDIR ?= /lib/modules/$(shell uname -r)/build
FHS_CEC_VERSION := $(shell cat $(CURDIR)/VERSION)
TARBALL := fhs-cec-$(FHS_CEC_VERSION)
DIST_FILES := Kbuild Makefile VERSION fhs-cec.c fhs-cec-proto.h \
	70-fhs-cec.rules README.md LICENSE

.PHONY: all modules install clean dkms-tarball

all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(CURDIR) FHS_CEC_VERSION=$(FHS_CEC_VERSION) modules

install:
	$(MAKE) -C $(KDIR) M=$(CURDIR) FHS_CEC_VERSION=$(FHS_CEC_VERSION) modules_install
	depmod -a

clean:
	rm -rf *.o *.ko *.mod *.mod.c *.mod.o .*.cmd .*.d .tmp_versions \
		modules.order Module.symvers $(TARBALL) $(TARBALL).tar.gz

dkms-tarball:
	rm -rf $(TARBALL)
	mkdir $(TARBALL)
	cp $(DIST_FILES) $(TARBALL)/
	sed 's/@VERSION@/$(FHS_CEC_VERSION)/' dkms.conf > $(TARBALL)/dkms.conf
	COPYFILE_DISABLE=1 tar -czf $(TARBALL).tar.gz $(TARBALL)
	rm -rf $(TARBALL)
