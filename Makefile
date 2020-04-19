CC=gcc
INC=-I/usr/include/libusb-1.0
LIB=-L/lib/$(arch)-linux-gnu/libusb-1.0.so.0

.PHONY: all clean

obj-m += hanwang.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
