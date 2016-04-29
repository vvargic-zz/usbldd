#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/usb.h>
#include <linux/hid.h>

#define VENDOR_ID       0x0483
#define PRODUCT_ID      0x572b

#define USB_STM32_MINOR_BASE	20

static struct usb_device_id usb_stm32_id_table [] = {
	{ USB_DEVICE(VENDOR_ID, PRODUCT_ID)},
	//USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT, USB_INTERFACE_PROTOCOL_MOUSE) },
	{ }	/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, usb_stm32_id_table);


struct usb_stm32 {
	struct usb_device 	*udev;
	struct usb_interface 	*interface;
	struct semaphore	limit_sem;
	struct urb 		*irq_in_urb;
	__u8			irq_in_epAddr;
	spinlock_t		err_loc;
	struct mutex		io_mutex;
	signed char 		*data;
	dma_addr_t		data_dma;
};

static int usb_stm32_open(struct input_dev *dev)
{/*
	struct usb_mouse *mouse = input_get_drvdata(dev);

	mouse->irq->dev = mouse->usbdev;
	if (usb_submit_urb(mouse->irq, GFP_KERNEL))
		return -EIO;
*/
	return 0;
}

static void usb_stm32_close(struct input_dev *dev)
{/*
	struct usb_mouse *mouse = input_get_drvdata(dev);

	usb_kill_urb(mouse->irq);*/
}


static const struct file_operations stm32_fops = {
	.owner = 	THIS_MODULE,
	.open = 	usb_stm32_open,
	.release = 	usb_stm32_close,
};

static struct usb_class_driver stm32_class = {
	.name =		"stm32_usb",
	.fops =		&stm32_fops,
	//.minor_base =	USB_STM32_MINOR_BASE,
};

static void usb_stm32_irq(struct urb *urb)
{
	struct usb_stm32 *stm32 = urb->context;
	signed char *data = stm32->data;
	int status;
	
	switch (urb->status) {
	case 0: break;	// success
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	default:
		{
			printk(KERN_ALERT "URB callback error. Urb status FAIL.\n");
			return;
		}
	}
	
	
	printk(KERN_ALERT "Received data from USB STM32 device: %s", data);


	status = usb_submit_urb (urb, GFP_ATOMIC);
	if (status) printk(KERN_ALERT "Resubmit error.\n");
		/*dev_err(&mouse->usbdev->dev,
			"can't resubmit intr, %s-%s/input0, status %d\n",
			mouse->usbdev->bus->bus_name,
			mouse->usbdev->devpath, status);*/


}


static int usb_stm32_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_stm32 *stm32;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	
	
	int pipe, maxp;
	int error = -ENOMEM;
	
	stm32 = kzalloc(sizeof(struct usb_stm32), GFP_KERNEL);
	printk(KERN_ALERT "Entering probe.\n");
	
	stm32->udev = usb_get_dev(interface_to_usbdev(interface));
	stm32->interface = interface;
	
	iface_desc = interface->cur_altsetting;
	if (iface_desc->desc.bNumEndpoints != 1)
	{
		printk(KERN_ALERT "Failing bNumEndpoint desc.\n");
		return -ENODEV;
	}
	endpoint = &iface_desc->endpoint[0].desc;
	if (!usb_endpoint_is_int_in(endpoint))
	{
		printk(KERN_ALERT "Failing endpoint  desc.\n");
		return -ENODEV;
	}

	pipe = usb_rcvintpipe(stm32->udev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(stm32->udev, pipe, usb_pipeout(pipe));
	
	stm32->irq_in_epAddr = endpoint->bEndpointAddress;
	printk(KERN_ALERT "Endpoint address is: %d\n", stm32->irq_in_epAddr);
	

	stm32->irq_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!stm32->irq_in_urb)
	{
		printk(KERN_ALERT "Failed to allocate URB.\n");
		return error;
	}
	stm32->data = usb_alloc_coherent(stm32->udev, 8, GFP_ATOMIC, &stm32->data_dma);
	if (!stm32->data)
	{
		printk(KERN_ALERT "Failed to allocate DMA buffer.\n");
		return error;
	}
	
	usb_fill_int_urb(stm32->irq_in_urb, stm32->udev, pipe, stm32->data, 8, usb_stm32_irq, stm32, endpoint->bInterval);
	stm32->irq_in_urb->transfer_dma = stm32->data;
	stm32->irq_in_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	
	usb_set_intfdata(interface, stm32);
	error = usb_register_dev(interface, &stm32_class);
	if(error) {
		printk(KERN_ALERT "Not able to register device.\n");	
	}
	
	//dev_info(&interface->dev,"USB Skeleton device now attached to USBSkel-%d", interface->minor);

	return 0;
}

static void usb_stm32_disconnect(struct usb_interface *intf)
{
	struct usb_stm32 *stm32 = usb_get_intfdata (intf);

	usb_set_intfdata(intf, NULL);
	usb_deregister_dev(intf, &stm32_class);
	
	
	stm32->interface = NULL;
	
	if (stm32) {
		usb_kill_urb(stm32->irq_in_urb);
		usb_free_urb(stm32->irq_in_urb);
		usb_free_coherent(interface_to_usbdev(intf), 8, stm32->data, stm32->data_dma);
		kfree(stm32);
	}
	
	printk(KERN_ALERT "Disconnecting STM32 USB.");
}



static struct usb_driver usb_stm32_driver = {
	.name		= "STM32 USB driver",
	.probe		= usb_stm32_probe,
	.disconnect	= usb_stm32_disconnect,
	.id_table	= usb_stm32_id_table,
};
//module_usb_driver(usb_stm32_driver);

/************************** INIT FUNCITON **************************************/

static int __init usb_stm32_init(void)
{
	int retval = 0;

	retval = usb_register(&usb_stm32_driver);
	if (retval) printk(KERN_ALERT "usb_register failed. Error number %d", retval);
	return retval;
}
/********************************* EXIT FUNCITON *******************************/
static void __exit usb_stm32_exit(void)
{
	usb_deregister(&usb_stm32_driver);
}

module_init (usb_stm32_init);
module_exit (usb_stm32_exit);


MODULE_AUTHOR("Vjeks");
MODULE_DESCRIPTION("STm32 USB driver");
MODULE_LICENSE("GPL");

