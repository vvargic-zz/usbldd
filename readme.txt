lfs_ppd01 1.2
Workspace and repository created
kernel version: 2.6.32.60

1.3 Building the lfs_ppd01

	1. Which logging daemon is used on your system?
	
	- In multitasking computer operating systems, a daemon (/ˈdiːmən/ or /ˈdeɪmən/)[1] is a computer program that runs as a background process, rather than being under the direct control of an interactive user. Traditionally daemon names end with the letter d: for example, syslogd is the daemon that implements the system logging facility and sshd is a daemon that services incoming SSH connections. (Source: wikipedia)

	- Logging daemong used on this system is syslogd


	2. What are all files used for logging?
	
	- daemon.log, debug, kern.log, messages, syslog

	-> daemon.log = KERN_EMERG 
	-> debug = KERN_EMERG, KERN_DEBUG
	-> kern.log = KERN_EMERG, KERN_ALERT, KERN_CRIT, KERN_ERR, KERN_WARNING, KERN_NOTICE, KERN_INFO, KERN_DEBUG
	-> messages = KERN EMERG, KERN_WARNING, KERN_NOTICE, KERN_INFO
	-> syslog = KERN_EMERG, KERN_ALERT, KERN_CRIT, KERN_ERR, KERN_WARNING, KERN_NOTICE, KERN_INFO, KERN_DEBUG
	

	3. Which file is better monitor and why

	- Depends on what we want to see
	- Syslog shows all the messages


	4. What is the difference between driver and device?

	- Device refers to physical device, hardware which we manage through registers in memory
	- Driver is code, software, with which we control the devices


	5. What was the name of processes in modul init and module exit? Explain these.
	- Name of the process in module init was "insmod" and in module exit "rmmod"
	- Those are the names of the kernel processes that were happening at the time, to load and unload our module


	6. Which way you used to monitor logs?

	- By printing on the terminal with KERN_EMERG and occasionaly checking syslog








	Along with the answers, write which file were introduced to solve the task and write instructions how to build , load driver and create device.

	- Current directory consist of three files: lfs_ppd01.c, Makefile, load_lfs

	- BUILDING: to build file Makefile was written. Makefile checks if our module is in kernel source tree. If it is it just states that there is one module to be built from the object file. If it is not then it uses the power of extend GNU "make" syntax to tell the compiler where to find kernel source tree and where to find our module to compile.

	Code for appropriate Makefile is:
		ifneq($(KERNELRELEASE),)
			obj-m := lfs_ppd01.o
		else
			KERNELDIR ?= /lib/modules/$(shell uname -r)/build
			PWD := $(shell pwd)
		default:
			$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
		endif


	- LOADING DRIVER AND CREATING DEVICE: to load driver and create device shell script load_lfs was written. It reads module's major number from /proc/devices and makes nod in /dev with wanted device name and major number that was read. It also changes mode for that device so that it can be written by anyone.
	
	Code for load_lfs is:
		module="lfs_ppd01"
		device="lfs_ppd01"
		mode="664"

		/sbin/insmod ./$module.ko $* || exit 1
		rm -f /dev/$device
		major=$(awk "\$2==\"$module\" {print \$1}" /proc/devices)
		mknod /dev/$device c $major 0 
		chmod $mode /dev/$device

	- After writing those scripts mode is loaded with:".../trunk load_lfs"
	- Unloading module is done with:"rmmod lfs_ppd01"




	








1.4. Making the LEDs Glow


	- DISABLING DRIVERS FOR PARALLEL PORT: On every system reboot modules that use parallel port were auto loaded. Those modules are lp, ppdev, parport and parport_pc. In order to disable auto loading of those modules additional lines were written in file /etc/modprobe.d/blacklist.conf. Here are those lines:
	
		blacklist lp
		blacklist ppdev
		blacklist parport_pc
		blacklist parport

	- MAKING LEDs GLOW: To make LEDs glow control of parallel port register was requested and byte of data was written to that register. Here are used functions:

		request_region(base, 1, name); // taking control of parallel port memory register where base represents base address of that port. Address in this case is 0x378. Name is the name that we want to give it to device.

		outb(pombyte, base); // function for writing pombyte (byte of data) to register at base address (parallel port register, 0x378)
	
		release_region(base, 1); // releasing control of parallel port register at base address


	- MAKING DIFFERENT LEDs GLOW: To make different LEDs glow, without the need to recompile module everytime, module parameters were used. The place were that paramater is to be found is bash script called "load_ppd01". Name of the parameter is "leds". Line in which that paramater can be written is line with insmod function. Here is the example of changing that line in a way that byte 0x01 is written to LEDs:

		/sbin/insmod ./$module.ko leds=0x01 $* || exit 1
	
	After that change, script "load_ppd01" needs to be executed and new data will be written to parallel port.





	1. What is the actual difference between I/O port memory region and memory region? Why do we need at all port region, why aren't they simply mapped in memory space?
	
	- Difference between I/O port memory region and memory region is actual physical difference between those address spaces, meaning that those regions are physicaly separated and accessed with different functions.

	- Reasons why we should need separate address space for I/O port region is that in that case we have more memory space to use and there is no fear of accidentally writing to devices.

	- Reasons why some proccessors use same address space for ports and memory, where I/O ports are simply mapped in memory space, are simplicity beacause same functions are used both for memory access and for I/O port access and the fact that I/O operations are only small fraction of the operations performed by a computer system and it may not be worthwile to support such infrequent operations with a rich instruction set.



	2. Do you know of some architectures which separates I/O control in separate instructions? Do you know of some architectures which observe I/O ports just as plain memory space?


	- x86 architecture has different functions to access memory and to control I/O. For example, assembly instruction to write something in memory is MOV and assembly instruction to write something to I/O port is OUT.

	- ARM architecture observe I/O ports just as plain memory space. In ARM there is no concept of a separate I/O address space and peripheral devices are read and written as if they were areas of memory. 



	3. Which function will you use to access IO port-mapped region?

	- Function to acces IO port-mapped region is
		
		struct resource *request_region(unsigned long first, unsigned long n, const char *name);
	 
	   where first is the base address of port, n is number of consecutive port registers that we want to use and name is name of the device.

	- Also, after module is done with I/O ports they should be freed with function
		
		void release_region(unsigned long first, unsigned long n);


	4. Which function will you use to access IO memory mapped region?

	- Function to access IO memory mapped region is
		
		struct resource *request_mem_region(unsigned long start, unsigned long len, char *name);
	
	  That functions allocates a memory region of len bytes, starting at start.
	
	- After module is done with that memory region it should be freed with
	
		void release_mem_region(unsigned long start, unsgined long n);

	
	
	5. For abowe two functions, what are the files in /proc which can be used to tract current allocation by various system components?

	- All I/O port allocations show up in /proc/ioports and all I/O memory allocations are lsited in /proc/iomem.

	
	6. Write function which access port number 100.

	- void accessPort (void)
	  {
	  	if(request_region(0x100, 1, "port100"))
			outb(0x01, 0x100);
	  }

	
	7. Write function which accesses memory address 100.

	- void accessMemory (void)
	  {
	  	void *adr;

		request_mem_region(0x100, 1, "memory100");
		adr=ioremap(base,1);
		iowrite8(0x01,adr);
	  }	
	
	
	

































