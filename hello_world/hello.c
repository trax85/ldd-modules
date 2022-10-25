#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

MODULE_LICENSE("Dual BSD/GPL");

static short int getint=1;
static char *myname="Tejas";

module_param(getint,short,0);
MODULE_PARM_DESC(getint,"Takes a short int");	//Document the above variable
module_param(myname,charp,S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(charp,"Takes a string");

static int hello_start(void){
	printk(KERN_ALERT"Hello %s\n",myname);
	return 0;
}

static void hello_end(void){
	printk(KERN_ALERT"Bye,World!\n");
}

module_init(hello_start);
module_exit(hello_end);
