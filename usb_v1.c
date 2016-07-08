#include <linux/kernel.h>
#include <linux/types.h>
//#include <linux/kdev_t>
#include <linux/fs.h>
#include <linux/sched.h>
//#include <linux/proc_fs>
#include <linux/moduleparam.h>
#include <asm/io.h>
//#include <asm/system.h>
#include <linux/ioport.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/ioctl.h>
#include <linux/fcntl.h>
#include <linux/random.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/kref.h>
#include <linux/mutex.h>

#define VENDOR_ID       0x0483
#define PRODUCT_ID      0x572b

#define USB_STM32_MINOR_BASE	0

#define INT_EP

#define BUFFER_SIZE 800

dev_t dev_no;
char *name = "stm32 usb driver";
struct cdev *usb_stm32_cdev;
static struct usb_driver usb_stm32_driver;

static struct usb_device_id usb_stm32_id_table [] = {
	{ USB_DEVICE(VENDOR_ID, PRODUCT_ID)},
	{ }	/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, usb_stm32_id_table);

static DEFINE_MUTEX(io_mutex);
struct file *randfd;
int storedValuesIndex = 0;
int flag = 1;

struct {
	int entropy_count;
	int buf_size;
	uint8_t buf[BUFFER_SIZE];
} entropy;

struct usb_stm32 {
	struct usb_device 	*udev;
	struct usb_interface 	*interface;
	unsigned char		minor;
	
	struct semaphore	sem;
	spinlock_t		spinlock;
	int			open_count;
	bool			ongoing_read;
	bool			read_flag;
	struct kref		kref;
	wait_queue_head_t	int_in_wait;
	size_t			int_in_filled;
	size_t			int_in_copied;
	
	unsigned char				*int_in_buffer;
	struct usb_endpoint_descriptor	*int_in_endpoint;
	struct urb 		*int_in_urb;	
	__u8			irq_in_epAddr;
	
	signed char 		*data;
};

// I'm really bad at putting functions in right order :/
static int usb_stm32_read_io (struct usb_stm32 *dev);

static void usb_stm32_delete(struct kref *kref)
{	
	struct usb_stm32 *stm32 = container_of(kref, struct usb_stm32, kref);
	
	usb_kill_urb(stm32->int_in_urb);
	usb_free_urb(stm32->int_in_urb);
	kfree(stm32->int_in_buffer);
	kfree(stm32);
}		

static int usb_stm32_open(struct inode *inode, struct file *file)
{
	struct usb_stm32 *dev;
	struct usb_interface *interface;
	int minor;

	printk (KERN_NOTICE "Process (pid %i), major %d, minor %d is trying to open...\n", current->pid, MAJOR(dev_no), MINOR(dev_no));
	minor = iminor(inode);
	
	interface = usb_find_interface(&usb_stm32_driver, minor);
	if( !interface )
	{
		printk(KERN_ALERT "Can't find device for minor %d.\n", minor);
		return -1;
	}
	
	dev = usb_get_intfdata(interface);
	if (!dev)
	{
		printk(KERN_ALERT "No dev interface.\n");
		return -1;
	}
	
	/*if( usb_autopm_get_interface(interface))
	{
		printk(KERN_ALERT "Autopm failure.\n");
		return -1;
	}*/
	
	kref_get(&dev->kref);	
	file->private_data = dev;
	
	printk(KERN_ALERT "Device opened.\n");

	return 0;
}

static int usb_stm32_release(struct inode *inode, struct file *file)
{
	struct usb_stm32 *stm32;
	
	printk(KERN_NOTICE "Releasing file, major: %d, minor: %d.\n", MAJOR(dev_no), MINOR(dev_no));
	stm32 = file->private_data;	
	if(!stm32)
	{
		printk(KERN_ALERT "Dev pointer is NULL.\n");
		return -1;
	}
	
	mutex_lock(&io_mutex);
	if(stm32->interface)
	{
		usb_autopm_put_interface(stm32->interface);
	}
	mutex_unlock(&io_mutex);
	kref_put(&stm32->kref, usb_stm32_delete);
	
	return 0;
}

ssize_t usb_stm32_read(struct file *file, char __user *buff, size_t count, loff_t *f_pos)
{
	struct usb_stm32 *dev;
	int retval;
	bool ongoing_io;	// it's just different name for ongoing_read really
	size_t available;
	size_t chunk;
	
	dev = file->private_data;
	
	// if can't read at all, why bother
	if(!dev->int_in_urb || !count)
	{	
		return 0;
	}
	printk(KERN_ALERT "\n1.\n");
	// one at the time, please
	retval = mutex_lock_interruptible(&io_mutex);
	// if we can't lock this, we will not continue
	
	printk(KERN_ALERT "\n2.\n");
	if(retval)
	{
		return retval;
	}
	
	if(!dev->interface)
	{
		printk(KERN_ALERT "Disconnect was called.\n");
		return -1;
	}
	printk(KERN_ALERT "\n3.\n");
pokusaj_ponovo:
	// If module is handling IO currently, do not interfere
	spin_lock_irq(&dev->spinlock);
	ongoing_io = dev->ongoing_read;
	spin_unlock(&dev->spinlock);
	
	printk(KERN_ALERT "\n4.\n");
	if (ongoing_io)
	{
		// if we have nonblocking IO, let it go
		/*if(file->f_flags && O_NONBLOCK)
		{
			mutex_unlock(&io_mutex);
			// notice that we don't handle error return values very well
			return -1; 
		}*/
		printk(KERN_ALERT "\n5.\n");
		// if IO wants to wait, give him what he wants
		retval = wait_event_interruptible(dev->int_in_wait,(!dev->ongoing_read));
		if(retval)
		{
			mutex_unlock(&io_mutex);
			// I should have implemented goto 'error'
			return -1;
		}
	}
	// okay, now we can talk
	printk(KERN_ALERT "\n6.\n");
	// if we have data, read it
	if(dev->int_in_filled)
	{
		available = dev->int_in_filled - dev->int_in_copied;
		chunk = min(available,count);
		printk(KERN_ALERT "\n7.\n");
		if(!available)
		{
			// if all data is used, we need IO
			retval = usb_stm32_read_io(dev);
			if(retval)
			{
				mutex_unlock(&io_mutex);
				return -1;
			}
			else
			{
				goto pokusaj_ponovo;
			}
		}
		printk(KERN_ALERT "\n8.\n");
		if( copy_to_user(buff,dev->int_in_buffer + dev->int_in_copied, chunk))
		{
			retval = -1;
		}
		else
		{
			retval = chunk;
		}
		// if they want more, start new reading
		if(available < count)
		{
			printk(KERN_ALERT "\n9.\n");
			usb_stm32_read_io(dev);
			printk(KERN_ALERT "\n10.\n");
		}
	}
	else
	{
		// no data in buffer
		printk(KERN_ALERT "\n11.\n");
		retval = usb_stm32_read_io(dev);
		printk(KERN_ALERT "\n12.\n");
		if(retval)
		{
			mutex_unlock(&io_mutex);
			return -1;
		}
		else
		{
			goto pokusaj_ponovo;
		}
	}	
	printk(KERN_ALERT "\n13.\n");
	mutex_unlock(&io_mutex);
	return retval;
}

static struct file_operations stm32_fops = {
	.owner = 	THIS_MODULE,
	.open = 	usb_stm32_open,
	.release = 	usb_stm32_release,
	.read = 	usb_stm32_read,
};

static struct usb_class_driver stm32_class = {
	.name =		"stm32_usb",
	.fops =		&stm32_fops,
	.minor_base =	USB_STM32_MINOR_BASE,
};

// Not to be used.
// It is strongly not recommended to work with files from kernel space.
// The function mixes random bytes in the random entropy_pool
// through the RNDADDENTROPY wrapper.
void write_entropy(void)
{
	mm_segment_t old_fs;
	
	entropy.entropy_count = BUFFER_SIZE*8;
	entropy.buf_size = BUFFER_SIZE;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	
	randfd = filp_open("/dev/random", O_WRONLY, 0/*0644*/);
	if (randfd == NULL)
	{
		printk(KERN_ALERT "Could not access entropy_pool.\n");
		return;
	}
	
	randfd->f_op->unlocked_ioctl(randfd, RNDADDENTROPY, &entropy);
	
	filp_close(randfd, NULL);
	set_fs(old_fs);
}

static void usb_stm32_int_callback(struct urb *urb)
{
	struct usb_stm32 *dev = urb->context;
	int retval, i;
	printk(KERN_ALERT "\n14.\n");
	spin_lock(&dev->spinlock);	
	if (urb->status)
	{
		printk(KERN_ALERT "Problem with received data (urb error).\n");
	}
	else
	{
		dev->int_in_filled = urb->actual_length;
	}
	
	dev->ongoing_read = 0;
	spin_unlock(&dev->spinlock);
	printk(KERN_ALERT "\n15.\n");
	// Wake up those who wait on reading IN data 
	//(ongoing_read signal to equals 0) for we are done.
	wake_up_interruptible(&dev->int_in_wait);	
	printk(KERN_ALERT "\n16.\n");
	/*if (urb->actual_length > 0)
	{
		for(i=0;i<urb->actual_length;i++)
		{

			printk(KERN_ALERT " %d,",(uint8_t)dev->int_in_buffer[i]);
	
			//storedValues[storedValuesIndex] = (uint8_t)dev->int_in_buffer[i];
			entropy.buf[storedValuesIndex] = (uint8_t)dev->int_in_buffer[i];
			storedValuesIndex++;
			if ( !(storedValuesIndex %= BUFFER_SIZE) ) flag = 1;
			
			
		}
	}
	printk(KERN_ALERT "\n");*/
	
	//mix_pool_bytes(struct entropy_store *r,storedValues,4);
	//credit_entropy_bits(struct entropy_store *r,4);
	
	/*if (flag)
	{
		//write_entropy();
		flag = 0;
	}
	
	retval = usb_submit_urb (dev->int_in_urb, GFP_ATOMIC);
	if (retval) printk(KERN_ALERT "Int callback resubmit error.\n");*/
}

static int usb_stm32_read_io (struct usb_stm32 *dev)
{
	int retval;
	printk(KERN_ALERT "\n17.\n");
	// we are working here, do not disturb
	spin_lock(&dev->spinlock);
	dev->ongoing_read = 1;
	spin_unlock(&dev->spinlock);
	printk(KERN_ALERT "\n18.\n");
	// since we are here to submit URB,
	// that means that currently there is no data
	dev->int_in_filled = 0;
	dev->int_in_copied = 0;
	
	//submit URB
	retval = usb_submit_urb(dev->int_in_urb, GFP_ATOMIC);
	if (retval)
	{
		printk(KERN_ALERT "Submitting int urb failed.\n");
		// we failed, accept it and move on
		spin_lock(&dev->spinlock);
		dev->ongoing_read = 0;
		spin_unlock(&dev->spinlock);
	}
	printk(KERN_ALERT "\n19.\n");
	return retval;
}



static int usb_stm32_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_stm32 *dev = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpointI;
	int int_end_size;	
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
		return -1;
	}
	
	kref_init(&dev->kref);
	sema_init(&dev->sem,1);
	spin_lock_init(&dev->spinlock);
	init_waitqueue_head(&dev->int_in_wait);
	
	dev->udev = udev;
	dev->interface = interface;
	iface_desc = interface->cur_altsetting;
	
	if (iface_desc->desc.bNumEndpoints != 1)
	{
		printk(KERN_ALERT "Failing bNumEndpoint desc.\n");
		return -ENODEV;
	}
	

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
		printk(KERN_ALERT "Could not allocate in_buffer.\n");
		return -1;
	}
	
	dev->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	
	if (!dev->int_in_urb)
	{
		printk(KERN_ALERT "Could not allocate int_in_urb.\n");
		return -1;
	}
	
	usb_fill_int_urb(dev->int_in_urb,dev->udev, 
			usb_rcvintpipe(dev->udev,
					dev->int_in_endpoint->bEndpointAddress),
			dev->int_in_buffer,
			le16_to_cpu(dev->int_in_endpoint->wMaxPacketSize),
			usb_stm32_int_callback, dev,
			dev->int_in_endpoint->bInterval);
			
	/*retval = usb_submit_urb(dev->int_in_urb, GFP_ATOMIC);
	if (retval)
	{
		printk(KERN_ALERT "Submitting int urb failed.\n");
	}*/

	usb_set_intfdata(interface, dev);
	
	retval = usb_register_dev(interface, &stm32_class);
	if (retval)
	{
		printk(KERN_ALERT "Not able to register device.\n");
		usb_stm32_delete(&dev->kref);
		return -1;
	}
	
	dev->minor = interface->minor;
	printk(KERN_ALERT "STm32 RNG successfully attached to /dev/usb_stm32_driver.\n");
	
	return 0;
}

static void usb_stm32_disconnect(struct usb_interface *interface)
{
	struct usb_stm32 *stm32;
	int minor = interface->minor;
	
	stm32 = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);
	
	usb_deregister_dev(interface, &stm32_class);
	
	mutex_lock(&io_mutex);
	stm32->interface = NULL;
	mutex_unlock(&io_mutex);
	
	kref_put(&stm32->kref, usb_stm32_delete);
	printk(KERN_ALERT "Disconnecting STm32 RNG.\n");
}

static struct usb_driver usb_stm32_driver = {
	.name		= "STM32usb",
	.probe		= usb_stm32_probe,
	.disconnect	= usb_stm32_disconnect,
	.id_table	= usb_stm32_id_table,
};

int usb_stm32_info(char *page, char **start, off_t offset, int count, int *eof, void *data)
{
	unsigned int len;
	len = sprintf(page, "\nDriver %s, version 1.3; Author: Vjeko, %d.\n", name, MAJOR(dev_no));
	return len;
}

/************************** INIT FUNCITON **************************************/

static int __init usb_stm32_init(void)
{
	int retval = 0;
	unsigned int registracija;
	
	registracija = alloc_chrdev_region(&dev_no, 0, 1, name);
	if(registracija < 0)
	{
		printk(KERN_ALERT "Pribavljanje upravljackih brojeva neuspjesno. \n");
	}
	else
	{
		printk(KERN_ALERT "Ola! The process is \"%s\" (pid %i)\n", current->comm, current->pid);
	}
		
	//create_proc_read_entry("usb_stm32", 0, NULL, usb_stm32_info, NULL);
	//proc_create_data("usb_stm32", 0, NULL, &stm32_fops, NULL);

	
	usb_stm32_cdev = cdev_alloc();
	usb_stm32_cdev->ops = &stm32_fops;
	usb_stm32_cdev->owner = THIS_MODULE;
	if(cdev_add(usb_stm32_cdev, dev_no, 1) < 0) printk(KERN_ALERT "Neuspjesno dodavanje cdev strukture.\n");

	//device_create(&stm32_class, usb_stm32->udev, dev_no, NULL, "stm32", 1); 	

	retval = usb_register(&usb_stm32_driver);
	if (retval) printk(KERN_ALERT "usb_register failed. Error number %d", retval);
	return retval;
}
/********************************* EXIT FUNCITON *******************************/
static void __exit usb_stm32_exit(void)
{
	usb_deregister(&usb_stm32_driver);
	cdev_del(usb_stm32_cdev);
	
	//remove_proc_entry("usb_stm32", NULL);
	
	unregister_chrdev_region(dev_no,1);
	
	printk(KERN_ALERT "Adeus!\n");
}

module_init (usb_stm32_init);
module_exit (usb_stm32_exit);


MODULE_AUTHOR("Vjekoslav Vargic");
MODULE_DESCRIPTION("STm32 RNG USB driver");
MODULE_LICENSE("GPL");

