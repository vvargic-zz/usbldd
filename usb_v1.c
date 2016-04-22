#include <linux/module.h>	// required header
#include <linux/init.h>		// module_init and _exit
#include <linux/kernel.h>	// printk
#include <linux/types.h>	// dev_t type
#include <linux/kdev_t.h>	// major and minor macros	

#include <linux/fs.h>		// for Device Numbers functions
#include <linux/sched.h>	// to refer to current process
#include <linux/proc_fs.h>	// to work with /proc
#include <linux/moduleparam.h>	// for module parameters
#include <linux/ioport.h>	// resource allocators for IO ports

#include <linux/cdev.h>		// cdev structure

static int __init hello_init(void)
{
	printk(KERN_ALERT "Hello, world\n");
	return 0;
}

static void __exit hello_exit(void)
{
	printk(KERN_ALERT "Bye, world\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Vjeko");
