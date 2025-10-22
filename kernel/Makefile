# Use := for immediate evaluation of shell command
KERNEL_SRC := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# The default 'all' target will build the modules.
.PHONY: all
# The default target
all: modules dtbs

.PHONY: modules
# The modules target. Builds the kernel module.
modules:
	$(MAKE) -C "$(KDIR)" M="$(PWD)" modules

.PHONY: dtbs
# The dtbs target, which depends on the module being built first
dtbs: modules
	dtc -I dts -O dtb -o $(PWD)/dts/simtemp.dtbo $(PWD)/dts/nxp-simtemp-overlay.dts

# The 'clean' target.
.PHONY: clean
clean:
	$(MAKE) -C "$(KDIR)" M="$(PWD)" clean
