#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>

void input_handler(){
	printf("Async notification recived\n");
}

void main(){
	int fd, oflags;
	
  	fd = open("/dev/ldd", O_RDWR, S_IRUSR | S_IWUSR);
	  if (fd !=  - 1) {
	    signal(SIGIO, input_handler);
	    fcntl(fd, F_SETOWN, getpid());
	    oflags = fcntl(fd, F_GETFL);
	    fcntl(fd, F_SETFL, oflags | FASYNC);
	    while(1) {
	    	sleep(100);
	    }
	  } else {
	    printf("device open failure\n");
	  }
}
