# enable debugging 
DEBUG =y

obj-m = scull.o
#KDIR := /home/nesara/BBB/board-support/linux-4.1.13+gitAUTOINC+8dc66170d9-g8dc6617
KDIR := /media/sf_UbuntuVM_Shared/Escrima_kernel_xiaomi_msm8956
all:
	make -C $(KDIR) M=$(shell pwd) modules
clean:
	make -C $(KDIR) M=$(shell pwd) clean
