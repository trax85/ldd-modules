#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

//we can use any of these in the following ioctl functions
#define LDDIO_MAGIC 'b'

#define LDDIO_RST_VAL _IO(LDDIO_MAGIC, 0)	
#define LDDIO_R_VAL _IOR(LDDIO_MAGIC, 1, int)
#define LDDIO_W_VAL _IOR(LDDIO_MAGIC, 2, int)
#define LDDIO_R_BUFFER _IOR(LDDIO_MAGIC, 3, int)
#define LDDIO_E_READVAL _IOW(LDDIO_MAGIC, 4, int) 
#define LDDIO_E_WRITEVAL _IOW(LDDIO_MAGIC, 5, int)

int main(){
	int fd;
	int number;char ch = 'n';
	printf("\nOpening Driver\n");
	fd = open("/dev/ldd", O_RDWR);
	if(fd < 0){
		printf("Cannot open device file\n");
		return 0;
	}
	ioctl(fd, LDDIO_R_VAL, &number);
	printf("Value of read index is:%d\n",number);
	ioctl(fd, LDDIO_W_VAL, &number);
	printf("Value of write index is:%d\n",number);
	printf("want to reset(y/n)?");
	scanf("%c",&ch);
	if(ch == 'y')
		ioctl(fd, LDDIO_RST_VAL, &number);
	close(fd);
	return 0;
}
