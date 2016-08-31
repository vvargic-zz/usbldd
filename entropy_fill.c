#include <stdio.h>
#include <sys/fcntl.h>
#include <asm/types.h>
#include <linux/random.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>

#define BUFFER_SIZE 256

typedef struct {
   int bit_count;
   int byte_count;          
   unsigned char buf[BUFFER_SIZE];
} entropy_t;

int main (int argc, char *argv[])
{
	int rnd_out, stm;
	//int c;
	size_t rnd_in;
	entropy_t ent;
	
	rnd_out = open("/dev/random", O_WRONLY);
	if(rnd_out < 0)
	{
		perror("RND: problem opening /dev/random.\n");
		exit(1);
	}
	/*
	ent_read = open("/proc/sys/kernel/random/entropy_avail", O_RDONLY);
	if(ent_read < 0)
	{
		perror("RND: problem opening /proc/sys/kernel/random/entropy_avail.\n");
		exit(1);
	}*/
	
	stm = open("/dev/stm32_usb", O_RDONLY);
	if(stm < 0)
	{
		printf("Could not open stm32_usb.\n");
		return 2;
	}
	
	rnd_in = read(stm,&ent.buf[0],BUFFER_SIZE);
	if(rnd_in < 0)
	{
		printf("Error while reading stm32_usb.\n");
	}
	else
	{
		ent.byte_count = BUFFER_SIZE;
		ent.bit_count = BUFFER_SIZE*8;
		ioctl(rnd_out, RNDADDENTROPY, &ent);
	}
	
	close(stm);
	close(rnd_out);
	
	return 0;
}
	
	
