#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/usb.h>
#include <linux/hid.h>

#define VENDOR_ID       0x0483
#define PRODUCT_ID      0x572b

#define USB_STM32_MINOR_BASE	0

#define BULK_EP

uint8_t globalLEDflag = 0;
uint32_t globalURBflag = 0;

static struct usb_device_id usb_stm32_id_table [] = {
	{ USB_DEVICE(VENDOR_ID, PRODUCT_ID)},
	//USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT, USB_INTERFACE_PROTOCOL_MOUSE) },
	{ }	/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, usb_stm32_id_table);


struct usb_stm32 {
	struct usb_device 	*udev;
	struct usb_interface 	*interface;
	unsigned char		minor;
	struct semaphore	limit_sem;
	
	char				*int_in_buffer;
	struct usb_endpoint_descriptor	*int_in_endpoint;
	
	char				*bulk_in_buffer;
	struct usb_endpoint_descriptor	*bulk_in_endpoint;
	
	struct urb 		*int_in_urb;
	struct urb		*bulk_in_urb;
	
	__u8			irq_in_epAddr;
	__u8			bulk_in_epAddr;
	
	spinlock_t		err_loc;
	struct mutex		io_mutex;
	signed char 		*data;
	dma_addr_t		data_dma;
};

static int usb_stm32_open(struct inode *inode, struct file *file)
{/*
	struct usb_mouse *mouse = input_get_drvdata(dev);

	mouse->irq->dev = mouse->usbdev;
	if (usb_submit_urb(mouse->irq, GFP_KERNEL))
		return -EIO;
*/
	return 0;
}

static int usb_stm32_release(struct inode *inode, struct file *file)
{/*
	struct usb_mouse *mouse = input_get_drvdata(dev);

	usb_kill_urb(mouse->irq);*/
	return 0;
}


static struct file_operations stm32_fops = {
	.owner = 	THIS_MODULE,
	.open = 	usb_stm32_open,
	.release = 	usb_stm32_release,
};

static struct usb_class_driver stm32_class = {
	.name =		"stm32_usb",
	.fops =		&stm32_fops,
	.minor_base =	USB_STM32_MINOR_BASE,
};

static void usb_stm32_bulk(struct urb *urb)
{
	struct usb_stm32 *dev = urb->context;
	int retval, i;
	//uint8_t *data = (uint8_t *)urb->transfer_buffer;
	
	printk(KERN_ALERT "Bulk callback called.\n");
	if (urb->status)
	{
		printk(KERN_ALERT "Problem with received data (urb error).\n");
	}
	
	if (urb->actual_length > 0)
	{
		for(i=0; i < (urb->actual_length); i++)
		{
			printk(KERN_ALERT " %d,",(uint8_t)dev->bulk_in_buffer[i]);
		}

	}
	else
	{
		printk(KERN_ALERT "Data less than 0.\n");
	}
	
	printk(KERN_ALERT "\n\n");
	
	retval = usb_submit_urb (dev->bulk_in_urb, GFP_ATOMIC);
	if (retval) printk(KERN_ALERT "Bulk callback resubmit error.\n");
}

static void usb_stm32_irq(struct urb *urb)
{
	struct usb_stm32 *dev = urb->context;
	int retval, i;
	//char *data = (char *)urb->transfer_buffer;

	//printk(KERN_ALERT "URB callback function.\n");
	
	globalURBflag++;
	
	
	if (urb->status)
	{
		printk(KERN_ALERT "Problem with received data (urb error).\n");
	}
	
	if (urb->actual_length > 0)
	{
		//printk(KERN_ALERT "Length of int_in_buffer is: %d\n", urb->actual_length);
		for(i=0;i<urb->actual_length;i++)
		{
			//printk(KERN_ALERT "Received data[%d] from USB STM32 device: %d\n", i,(uint8_t)dev->int_in_buffer[i]);
			printk(KERN_ALERT " %d,",(uint8_t)dev->int_in_buffer[i]);
		}
		/*for(i=0;i<urb->actual_length;i++)
		{ 
			printk(KERN_ALERT "Received data[%d]_v2 from USB STM32 dev: %d\n", i, *(data+i));
		}*/
	}
	printk(KERN_ALERT "\n\n");
	

	retval = usb_submit_urb (dev->int_in_urb, GFP_ATOMIC);
	//retval = usb_submit_urb(dev->bulk_out_urb, GFP_ATOMIC);
	if (retval) printk(KERN_ALERT "Int callback resubmit error.\n");
	
	/*if (globalURBflag == 10)
	{
		retval = usb_submit_urb(dev->bulk_out_urb, GFP_ATOMIC);
		if(retval) printk(KERN_ALERT "Bulk submitt ERROR");
		globalURBflag = 0;
	}*/

	
		/*dev_err(&mouse->usbdev->dev,
			"can't resubmit intr, %s-%s/input0, status %d\n",
			mouse->usbdev->bus->bus_name,
			mouse->usbdev->devpath, status);*/
}


static int usb_stm32_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_stm32 *dev = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpointI;
	struct usb_endpoint_descriptor *endpointB;
 
	int int_end_size;
	int bulk_in_size;
	
	int retval = -ENODEV;

	printk(KERN_ALERT "Entering STM32 probe.\n");
	if (!udev)
	{
		printk(KERN_ALERT "UDEV is NULL\n");
	}
	
	dev = kzalloc(sizeof(struct usb_stm32), GFP_KERNEL);
	if (!dev)
	{
		printk(KERN_ALERT "Could not allocate memory for struct usb_stm32.\n");
	}
	
	dev->udev = udev;
	dev->interface = interface;
	iface_desc = interface->cur_altsetting;
	
	if (iface_desc->desc.bNumEndpoints != 1)
	{
		printk(KERN_ALERT "Failing bNumEndpoint desc.\n");
		return -ENODEV;
	}
	
#ifdef INT_EP
	endpointI = &iface_desc->endpoint[0].desc;
	if (!usb_endpoint_is_int_in(endpointI))
	{
		printk(KERN_ALERT "Failing INT endpoint  desc.\n");
		return -ENODEV;
	}
	dev->int_in_endpoint = endpointI;
	int_end_size = le16_to_cpu(dev->int_in_endpoint->wMaxPacketSize);
	dev->int_in_buffer = kmalloc(int_end_size, GFP_KERNEL);
	
	if (!dev->int_in_buffer)
	{
		printk(KERN_ALERT "Int_in_buffer error.\n");
	}
	
	dev->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	
	if (!dev->int_in_urb)
	{
		printk(KERN_ALERT "Could not allocate int_in_urb.\n");
	}
	
	usb_fill_int_urb(dev->int_in_urb,dev->udev, 
			usb_rcvintpipe(dev->udev,
					dev->int_in_endpoint->bEndpointAddress),
			dev->int_in_buffer,
			le16_to_cpu(dev->int_in_endpoint->wMaxPacketSize),
			usb_stm32_irq, dev,
			dev->int_in_endpoint->bInterval);
			
	retval = usb_submit_urb(dev->int_in_urb, GFP_ATOMIC);
	if (retval)
	{
		printk(KERN_ALERT "Submitting int urb failed.\n");
	}
#endif	
#ifdef BULK_EP
	endpointB = &iface_desc->endpoint[0].desc;
	if (!usb_endpoint_is_bulk_in(endpointB))
	{
		printk(KERN_ALERT "Failing BULK endpoint  desc.\n");
		return -ENODEV;
	}
	dev->bulk_in_endpoint = endpointB;
	bulk_in_size = le16_to_cpu(dev->bulk_in_endpoint->wMaxPacketSize);
	dev->bulk_in_buffer = kmalloc(bulk_in_size, GFP_KERNEL);
	
	if (!dev->bulk_in_buffer)
	{
		printk(KERN_ALERT "Bulk_in_buffer error.\n");
	}
	
	dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	
	if (!dev->bulk_in_urb)
	{
		printk(KERN_ALERT "Could not allocate bulk_out_urb.\n");
	}
	
	usb_fill_bulk_urb(dev->bulk_in_urb,dev->udev, 
		usb_rcvbulkpipe(dev->udev,
				dev->bulk_in_endpoint->bEndpointAddress),
		dev->bulk_in_buffer,
		bulk_in_size,
		usb_stm32_bulk, dev);
		
	retval = usb_submit_urb(dev->bulk_in_urb, GFP_ATOMIC);
	if (retval)
	{
		printk(KERN_ALERT "Submitting bulk urb failed.\n");
	}
#endif	

	usb_set_intfdata(interface, dev);
	
	retval = usb_register_dev(interface, &stm32_class);
	if (retval)
	{
		printk(KERN_ALERT "Not able to register device.\n");
	}
	
	dev->minor = interface->minor;
	return 0;
}

static void usb_stm32_disconnect(struct usb_interface *interface)
{
	struct usb_stm32 *stm32;
	//int minor;

	//usb_set_intfdata(intf, NULL);
	//usb_deregister_dev(intf, &stm32_class);
	
	stm32 = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);
	
	usb_deregister_dev(interface, &stm32_class);
	
	
	stm32->interface = NULL;
	
	if (stm32) {
		usb_kill_urb(stm32->int_in_urb);
		usb_free_urb(stm32->int_in_urb);
		usb_kill_urb(stm32->bulk_in_urb);
		usb_free_urb(stm32->bulk_in_urb);
		//usb_free_coherent(interface_to_usbdev(intf), 8, stm32->data, stm32->data_dma);
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

