/*
 *  USB Hanwang tablet support
 *
 *  Copyright (c) 2010 Xing Wei <weixing@hanwang.com.cn>
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>

#define DRIVER_AUTHOR   "Aidan Walton <aidan@wires3.net>"
#define DRIVER_DESC     "USB Bosto(2nd Gen) tablet driver"
#define DRIVER_LICENSE  "GPL"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

#define USB_VENDOR_ID_HANWANG		0x0b57
#define USB_PRODUCT_BOSTO22HD		0x9016
#define USB_PRODUCT_BOSTO14WA		0x9018
#define USB_PRODUCT_ART_MASTER_III	0x852a
#define USB_PRODUCT_ART_MASTER_HD	0x8401

#define HANWANG_TABLET_INT_CLASS	0x0003
#define HANWANG_TABLET_INT_SUB_CLASS	0x0001
#define HANWANG_TABLET_INT_PROTOCOL	0x0002

#define PKGLEN_MAX	10

/* device IDs */
#define STYLUS_DEVICE_ID	0x02
#define TOUCH_DEVICE_ID		0x03
#define CURSOR_DEVICE_ID	0x06
#define ERASER_DEVICE_ID	0x0A
#define PAD_DEVICE_ID		0x0F

/* match vendor and interface info */
/* #define HANWANG_TABLET_DEVICE(vend, prod, cl, sc, pr) \
	.match_flags = USB_DEVICE_ID_MATCH_VENDOR \
		| USB_DEVICE_ID_MATCH_INT_INFO, \
	.idVendor = (vend), \
	.idProduct = (prod), \
	.bInterfaceClass = (cl), \
	.bInterfaceSubClass = (sc), \
	.bInterfaceProtocol = (pr)
*/

/* #define HANWANG_TABLET_DEVICE(vend, prod, cl, sc, pr) \
    .match_flags = USB_DEVICE_ID_MATCH_INT_INFO \
            | USB_DEVICE_ID_MATCH_DEVICE, \
        .idVendor = (vend), \
        .idProduct = (prod), \
        .bInterfaceClass = (cl), \
        .bInterfaceSubClass = (sc), \
        .bInterfaceProtocol = (pr)
*/

#define HANWANG_TABLET_DEVICE(vend, prod, cl, sc, pr) \
    .match_flags = USB_DEVICE_ID_MATCH_INT_INFO \
            | USB_DEVICE_ID_MATCH_DEVICE, \
        .idVendor = (vend), \
        .idProduct = (prod), \
        .bInterfaceClass = (cl), \
        .bInterfaceSubClass = (sc), \
        .bInterfaceProtocol = (pr)

enum hanwang_tablet_type {
	HANWANG_BOSTO_22HD,
	HANWANG_BOSTO_14WA,
	HANWANG_ART_MASTER_III, 
	HANWANG_ART_MASTER_HD,
};

struct hanwang {
	unsigned char *data;
	dma_addr_t data_dma;
	struct input_dev *dev;
	struct usb_device *usbdev;
	struct urb *irq;
	const struct hanwang_features *features;
	unsigned int current_tool;
	unsigned int current_id;
	char name[64];
	char phys[32];
};

struct hanwang_features {
	unsigned short pid;
	char *name;
	enum hanwang_tablet_type type;
	int pkg_len;
	int max_x;
	int max_y;
	int max_tilt_x;
	int max_tilt_y;
	int max_pressure;
};

static const struct hanwang_features features_array[] = {
	{ USB_PRODUCT_BOSTO22HD, "Bosto Kingtee 22HD", HANWANG_BOSTO_22HD,
		PKGLEN_MAX, 0x27de, 0x1cfe, 0x3f, 0x7f, 2048 },
	{ USB_PRODUCT_BOSTO14WA, "Bosto Kingtee 14WA", HANWANG_BOSTO_14WA,
		PKGLEN_MAX, 0x3d84, 0x2672, 0x3f, 0x7f, 2048 },
	{ USB_PRODUCT_ART_MASTER_III, "Hanwang Art Master III 1308", HANWANG_ART_MASTER_III,
		PKGLEN_MAX, 0x7f00, 0x4f60, 0x3f, 0x7f, 2048 },
	{ USB_PRODUCT_ART_MASTER_HD, "Hanwang Art Master HD 5012", HANWANG_ART_MASTER_HD,
		PKGLEN_MAX, 0x678e, 0x4150, 0x3f, 0x7f, 1024 },
};

static const int hw_eventtypes[] = {
	EV_KEY, EV_ABS, EV_MSC,
};

static const int hw_absevents[] = {
	ABS_X, ABS_Y, ABS_TILT_X, ABS_TILT_Y, ABS_WHEEL,
	ABS_RX, ABS_RY, ABS_PRESSURE, ABS_MISC,
};

static const int hw_btnevents[] = {
/*		BTN_STYLUS 		seems to be the same as a center mouse button above the roll wheel.
 	 	BTN_STYLUS2 	right mouse button (Gedit)
 	 	BTN_DIGI 		seems to do nothing in relation to the stylus tool 
		BTN_TOUCH		seems like a left mouse click, but all hell breaks loose depending on the application.*/
		
		BTN_DIGI, BTN_TOUCH, BTN_STYLUS, BTN_STYLUS2, BTN_TOOL_PEN, BTN_TOOL_BRUSH, BTN_TOOL_RUBBER, BTN_TOOL_PENCIL, BTN_TOOL_AIRBRUSH, BTN_TOOL_FINGER, BTN_TOOL_MOUSE
};

static const int hw_mscevents[] = {
	MSC_SERIAL,
};

static void hanwang_parse_packet(struct hanwang *hanwang)
{
	unsigned char *data = hanwang->data;
	struct input_dev *input_dev = hanwang->dev;
	struct usb_device *dev = hanwang->usbdev;
	enum hanwang_tablet_type type = hanwang->features->type;
	u16 x, y, p;
	
	dev_dbg(&dev->dev, "Bosto packet:  [B0:-:B8] %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n", data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9]);
	switch (data[0]) {
	case 0x02:			// Pen Event
		//printk (KERN_DEBUG "Pen Event Packet\n" );		// Pen Event as defined in hanvon driver.
		switch (data[1]) {
		case 0x80:	/* tool prox out */
			hanwang->current_id = 0;
			hanwang->current_tool = 0;
			input_report_key(input_dev, hanwang->current_tool, 0);
			input_report_key(input_dev, BTN_TOUCH, 0);
			dev_dbg(&dev->dev, "TOOL OUT. PEN ID:Tool %x:%x\n", hanwang->current_id, hanwang->current_tool );
			dev_dbg(&dev->dev, "Bosto packet:TOOL OUT  [B0:-:B8] %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n", data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9]);
			break;

		case 0xc2:													// first time tool prox in
			dev_dbg(&dev->dev, "TOOL IN: ID:Tool %x:%x\n", hanwang->current_id, hanwang->current_tool);
			dev_dbg(&dev->dev, "Bosto packet:TOOL IN  [B0:-:B8] %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n", data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9]);
				
			switch (data[3] & 0xf0) {
			case 0x20:												// Stylus Tip in prox. Bosto 22HD
				hanwang->current_id = STYLUS_DEVICE_ID;
				hanwang->current_tool = BTN_TOOL_PENCIL;
				input_report_key(input_dev, BTN_TOUCH, 0);
				input_report_key(input_dev, BTN_TOOL_PENCIL, 1);
				dev_dbg(&dev->dev, "TOOL IN:Exit ID:Tool %x:%x\n", hanwang->current_id, hanwang->current_tool );
				break;

			case 0xa0: // Stylus Eraser in prox. Bosto 2HD
				hanwang->current_id = ERASER_DEVICE_ID;
				hanwang->current_tool = BTN_TOOL_RUBBER;
				input_report_key(input_dev, BTN_TOUCH, 0);
				input_report_key(input_dev, BTN_TOOL_RUBBER, 1);
				dev_dbg(&dev->dev, "TOOL IN ERASER:Exit ID:Tool %x:%x\n", hanwang->current_id, hanwang->current_tool);
				break;
		
			default:
				hanwang->current_id = 0;
				dev_dbg(&dev->dev, "Unknown tablet tool %02x ", data[0]);
				break;
			}
			break;
			
		case 0xa0 ... 0xa3:			// Pen trackable but not in contact with screen.
			dev_dbg(&dev->dev, "PEN FLOATING ID:Tool %x:%x\n", hanwang->current_id, hanwang->current_tool );
			dev_dbg(&dev->dev, "Bosto packet:Float  [B0:-:B8] %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n", data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9]);
				
			x = (data[2] << 8) | data[3];		// Set x ABS
			y = (data[4] << 8) | data[5];		// Set y ABS
			p = 0;
			input_report_key(input_dev, BTN_TOUCH, 0);

			switch (data[1]) {
			case 0xa0 ... 0xa1:
				input_report_key(input_dev, BTN_STYLUS2, 0);
				break;
			case 0xa2 ... 0xa3:
				input_report_key(input_dev, BTN_STYLUS2, 1);
				break;
			}
			break;
		
		case 0xe0 ... 0xe3:		// Pen contact
								// All a little strange; these 4 bytes are always seen whenever the pen is in contact with the tablet. 'e0 + e1', without the stylus button pressed and 'e2 + e3' with the stylus button pressed. Either of the buttons.
								// in either case the byte value jitters between a pair of either of the two states dependent on the button press.
				dev_dbg(&dev->dev, "PEN TOUCH: ID:Tool %x:%x\n", hanwang->current_id, hanwang->current_tool );
				dev_dbg(&dev->dev, "Bosto packet:Touch  [B0:-:B8] %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n", data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9]);
				
				input_report_key(input_dev, BTN_TOUCH, 1);
				x = (data[2] << 8) | data[3];		// Set x ABS
				y = (data[4] << 8) | data[5];		// Set y ABS
				p = (data[6] << 3) | ((data[7] & 0xc0) >> 5) | (data[1] & 0x01);				// Set 2048 Level pressure sensitivity. 	NOTE: The stylus button, magnifies the pressure sensitivity 

				switch (data[1]) {
					case 0xe0 ... 0xe1:
						input_report_key(input_dev, BTN_STYLUS2, 0);
						break;
					case 0xe2 ... 0xe3:
						input_report_key(input_dev, BTN_STYLUS2, 1);
						break;
				}
				break;
		}
		break;
		
	case 0x0c:
		dev_dbg(&dev->dev,"Tablet Event. Packet data[0]: %02x\n", data[0] );		// Tablet Event as defined in hanvon driver. 
		input_report_abs(input_dev, ABS_MISC, hanwang->current_id);
		input_event(input_dev, EV_MSC, MSC_SERIAL, hanwang->features->pid);
		input_sync(input_dev);
		
	default:
		dev_dbg(&dev->dev, "Error packet. Packet data[0]:  %02x ", data[0]);
		break;
	}
	
	input_report_abs(input_dev, ABS_X, le16_to_cpup((__le16 *)&x));
	input_report_abs(input_dev, ABS_Y, le16_to_cpup((__le16 *)&y));
	input_report_abs(input_dev, ABS_TILT_X, 0x00);
	input_report_abs(input_dev, ABS_TILT_Y, 0x00);
	input_report_abs(input_dev, ABS_PRESSURE, le16_to_cpup((__le16 *)&p));
	input_report_abs(input_dev, ABS_MISC, hanwang->current_id);
	input_event(input_dev, EV_MSC, MSC_SERIAL, hanwang->features->pid);		
		
	input_sync(input_dev);
}

static void hanwang_irq(struct urb *urb)
{
	struct hanwang *hanwang = urb->context;
	struct usb_device *dev = hanwang->usbdev;
	int retval;

	switch (urb->status) {
	case 0:
		/* success */;
		hanwang_parse_packet(hanwang);
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_err(&dev->dev, "%s - urb shutting down with status: %d",
			__func__, urb->status);
		return;
	default:
		dev_err(&dev->dev, "%s - nonzero urb status received: %d",
			__func__, urb->status);
		break;
	}

	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&dev->dev, "%s - usb_submit_urb failed with result %d",
			__func__, retval);
}

static int hanwang_open(struct input_dev *dev)
{
	struct hanwang *hanwang = input_get_drvdata(dev);

	hanwang->irq->dev = hanwang->usbdev;
	if (usb_submit_urb(hanwang->irq, GFP_KERNEL))
		return -EIO;

	// printk(KERN_INFO "in hanwang open.\n" );
	return 0;
}

static void hanwang_close(struct input_dev *dev)
{
	struct hanwang *hanwang = input_get_drvdata(dev);

	usb_kill_urb(hanwang->irq);
}

static bool get_features(struct usb_device *dev, struct hanwang *hanwang)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(features_array); i++) {
		if (le16_to_cpu(dev->descriptor.idProduct) ==
				features_array[i].pid) {
			hanwang->features = &features_array[i];
			return true;
		}
	}

	return false;
}


static int hanwang_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint;
	struct hanwang *hanwang;
	struct input_dev *input_dev;
	int error;
	int i;

	printk (KERN_INFO "Bosto_Probe checking Tablet.\n");
	hanwang = kzalloc(sizeof(struct hanwang), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!hanwang || !input_dev) {
		error = -ENOMEM;
		goto fail1;
	}

	if (!get_features(dev, hanwang)) {
		error = -ENXIO;
		goto fail1;
	}

	hanwang->data = usb_alloc_coherent(dev, hanwang->features->pkg_len,
					GFP_KERNEL, &hanwang->data_dma);
	if (!hanwang->data) {
		error = -ENOMEM;
		goto fail1;
	}

	hanwang->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!hanwang->irq) {
		error = -ENOMEM;
		goto fail2;
	}

	hanwang->usbdev = dev;
	hanwang->dev = input_dev;

	usb_make_path(dev, hanwang->phys, sizeof(hanwang->phys));
	strlcat(hanwang->phys, "/input0", sizeof(hanwang->phys));

	strlcpy(hanwang->name, hanwang->features->name, sizeof(hanwang->name));
	input_dev->name = hanwang->name;
	input_dev->phys = hanwang->phys;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, hanwang);

	input_dev->open = hanwang_open;
	input_dev->close = hanwang_close;

	for (i = 0; i < ARRAY_SIZE(hw_eventtypes); ++i)
		__set_bit(hw_eventtypes[i], input_dev->evbit);

	for (i = 0; i < ARRAY_SIZE(hw_absevents); ++i)
		__set_bit(hw_absevents[i], input_dev->absbit);

	for (i = 0; i < ARRAY_SIZE(hw_btnevents); ++i)
		__set_bit(hw_btnevents[i], input_dev->keybit);

	for (i = 0; i < ARRAY_SIZE(hw_mscevents); ++i)
		__set_bit(hw_mscevents[i], input_dev->mscbit);

	input_set_abs_params(input_dev, ABS_X,
			     0, hanwang->features->max_x, 4, 0);
	input_set_abs_params(input_dev, ABS_Y,
			     0, hanwang->features->max_y, 4, 0);
	input_set_abs_params(input_dev, ABS_TILT_X,
			     0, hanwang->features->max_tilt_x, 0, 0);
	input_set_abs_params(input_dev, ABS_TILT_Y,
			     0, hanwang->features->max_tilt_y, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE,
			     0, hanwang->features->max_pressure, 0, 0);

	endpoint = &intf->cur_altsetting->endpoint[0].desc;
	usb_fill_int_urb(hanwang->irq, dev,
			usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			hanwang->data, hanwang->features->pkg_len,
			hanwang_irq, hanwang, endpoint->bInterval);
	hanwang->irq->transfer_dma = hanwang->data_dma;
	hanwang->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(hanwang->dev);
	if (error)
		goto fail3;

	usb_set_intfdata(intf, hanwang);

	return 0;

 fail3:	usb_free_urb(hanwang->irq);
 fail2:	usb_free_coherent(dev, hanwang->features->pkg_len,
			hanwang->data, hanwang->data_dma);
 fail1:	input_free_device(input_dev);
	kfree(hanwang);
	return error;

}

static void hanwang_disconnect(struct usb_interface *intf)
{
	struct hanwang *hanwang = usb_get_intfdata(intf);

	input_unregister_device(hanwang->dev);
	usb_free_urb(hanwang->irq);
	usb_free_coherent(interface_to_usbdev(intf),
			hanwang->features->pkg_len, hanwang->data,
			hanwang->data_dma);
	kfree(hanwang);
	usb_set_intfdata(intf, NULL);
}

static const struct usb_device_id hanwang_ids[] = {
	{ HANWANG_TABLET_DEVICE(USB_VENDOR_ID_HANWANG, USB_PRODUCT_BOSTO22HD, HANWANG_TABLET_INT_CLASS, HANWANG_TABLET_INT_SUB_CLASS, HANWANG_TABLET_INT_PROTOCOL) },
	{}
};



MODULE_DEVICE_TABLE(usb, hanwang_ids);

static struct usb_driver hanwang_driver = {
	.name		= "bosto_2g",
	.probe		= hanwang_probe,
	.disconnect	= hanwang_disconnect,
	.id_table	= hanwang_ids,
};

static int __init hanwang_init(void)
{
	printk(KERN_INFO "Bosto 2nd Generation USB Driver module being initialised.\n" );
	return usb_register(&hanwang_driver);
}

static void __exit hanwang_exit(void)
{
	usb_deregister(&hanwang_driver);
}

module_init(hanwang_init);
module_exit(hanwang_exit);
