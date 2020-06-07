CC=gcc
INC=-I/usr/include/libusb-1.0
LIB=-L/lib/$(arch)-linux-gnu/libusb-1.0.so.0 -lusb-1.0

.PHONY: all clean

obj-m += hanwang.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
	$(CC) bosto-usb-detach.c $(INC) $(LIB) -o bosto-usb-detach

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
	rm -f bosto-usb-detach

install:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules_install
	install -m 0755 bosto-usb-detach /usr/local/bin/
	install -m 0755 load_hanwang.sh /usr/local/bin/
	install -m 0644 hanwang.rules /etc/udev/rules.d/70-hanwang.rules
