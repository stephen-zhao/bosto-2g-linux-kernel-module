#!/bin/sh

logger udev rule triggered to load hanwang
rmmod hanwang
logger removed hanwang driver module from kernel

/usr/local/bin/bosto-usb-detach
logger detached usbhid driver module from hanwang device

insmod /lib/modules/$(uname -r)/hanwang.ko
