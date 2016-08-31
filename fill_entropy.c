// feed the random device. 
#include <stdio.h>
#include <sys/fcntl.h>
#include <asm/types.h>
#include <linux/random.h>

#define TRESHOLD 3596
#define MAX_ENTROPY 4096

typedef struct 
{ 
	int bit_count;
	int byte_count;
	char buf[MAX_ENTROPY];
} entropy_t; 

int main(int ac, char **av) 
{ 
	int rand_out, rand_in;
	size_t read_bytes;
	entropy_t entropy;
	int entropy_left;
	int entropy_needed;

	rand_out = open("/dev/random", O_WRONLY);
	if (rand_out < 0) 
	{
		printf("Can't open /dev/random.\n"); 
		exit(1);
	}
	
	rand_in = open("/dev/stm32_usb", O_RDONLY);
	if(rand_in < 0)
	{
		printf("Can't open /dev/stm32_usb.\n");
		exit(1);
	}
		
	if (daemon(1,1) < 0) 
	{
		printf("Can't daemonize :(.\n");
		exit(1);	
	}

	while (1) { 
		entropy_left = 0;
		ioctl(rand_out, RNDGETENTCNT, &entropy_left); 
		if (entropy_left <= TRESHOLD)  
		{
			entropy_needed = (int)MAX_ENTROPY - entropy_left;
			read_bytes = read(rand_in, &entropy.buf[0], entropy_needed);
			entropy.byte_count = read_bytes;
			entropy.bit_count = entropy.byte_count*8;
		
			if (ioctl(rand_out, RNDADDENTROPY, &entropy) < 0) 
			{
				printf("Error with filling entropy pool. Exiting.\n"); 
				exit(1); 
			}
		}
	}	
	close(rand_in);
	close(rand_out);  
} 
