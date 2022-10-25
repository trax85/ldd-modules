obj-m = hello.o
KDIR := /home/nesara/BBB/board-support/linux-4.1.13+gitAUTOINC+8dc66170d9-g8dc6617
all:
	make -C $(KDIR) M=$(shell pwd) modules
clean:
	make -C $(KDIR) M=$(shell pwd) clean
