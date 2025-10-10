# Use := for immediate evaluation of shell command
KERNEL_SRC := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# The default target
all: modules

# The modules target. Builds the kernel module.
modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

# A debug target that passes an extra CFLAG
debug:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) EXTRA_CFLAGS="-DDEBUG" modules

# The clean target
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean

# Declare phony targets to avoid conflicts with filenames
.PHONY: all modules dtbs clean debug
