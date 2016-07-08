#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
 
#define BUF_SIZE 20
int main(int argc, char* argv[]) {
 
    int input_fd;    /* Input and output file descriptors */
    ssize_t ret_in;    /* Number of bytes returned by read() and write() */
    char buffer[BUF_SIZE];
  
 
 
    /* Create input file descriptor */
    input_fd = open ("/dev/stm32_usb", O_RDONLY);
    if (input_fd == -1) 
    {
            printf("Could not open it!\n");
            return 2;
    }

    /* Copy process */
    ret_in = read (input_fd, &buffer, BUF_SIZE);
    if (ret_in < 0)
    {
    	printf("Error with reading.\n");
    }
    else
    {
    	printf("%d bytes read.\n", (int)ret_in);
    }
 
    /* Close file descriptors */
    if (close (input_fd)<0)
    {
    	printf("Problem with closing.\n");
    }
 
    return (EXIT_SUCCESS);
}
