# Use := for immediate evaluation of shell command
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# The default 'all' target will build the modules and DTBs.
.PHONY: all
# The default target
all: modules dtbs

# Check if the build command includes -DPC_BUILD, which signals DT-less compilation.
# EXTRA_CFLAGS is passed from the shell (e.g., via the build.sh script).
PC_BUILD_FLAG := $(shell echo $(EXTRA_CFLAGS) | grep -q 'DPC_BUILD' && echo 1 || echo 0)

.PHONY: modules
# The modules target. Builds the kernel module.
modules:
	$(MAKE) -C "$(KDIR)" M="$(PWD)" EXTRA_CFLAGS="$(EXTRA_CFLAGS)" modules

.PHONY: dtbs
# The dtbs target. Conditionally builds the Device Tree Overlay.
dtbs: modules
ifeq ($(PC_BUILD_FLAG), 0)
	@echo "Building Device Tree Overlay..."
	dtc -I dts -O dtb -o $(PWD)/dts/simtemp.dtbo $(PWD)/dts/nxp-simtemp-overlay.dts
	@echo "DT Overlay built successfully."
else
	@echo "Skipping Device Tree Overlay (PC_BUILD enabled)."
endif

# The 'clean' target.
.PHONY: clean
clean:
	$(MAKE) -C "$(KDIR)" M="$(PWD)" clean
	rm -f $(PWD)/dts/simtemp.dtbo
