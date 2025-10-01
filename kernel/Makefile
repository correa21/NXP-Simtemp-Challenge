export KERNEL_SRC = /lib/modules/$(shell uname -r)/build

all:
	mkdir -p build
	make -C $(KERNEL_SRC) src=$(PWD) M=$(PWD)/build modules
debug:
	mkdir -p build
	make -C $(KERNEL_SRC) src=$(PWD) M=$(PWD)/build EXTRA_CFLAGS+=-DDEBUG modules
clean:
	make -C $(KERNEL_SRC) src=$(PWD) M=$(PWD)/build clean
	rm -r ./build