CC=gcc
INC=-I/usr/include/libusb-1.0
LIB=-L/lib/$(arch)-linux-gnu/libusb-1.0.so.0 -lusb-1.0

.PHONY: all clean

obj-m += hanwang.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	$(CC) bosto-usb-detach.c $(INC) $(LIB) -o bosto-usb-detach

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f bosto-usb-detach
