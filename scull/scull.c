#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/types.h>		//Needed for typedef dev_t
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/fs.h>			//Needed for alloc ,register chrdev & fileops 
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include "scull.h"


int scull_major = SCULL_MAJOR;
int scull_minor = 0;
int scull_nr_devs = SCULL_NR_DEVS;
int scull_quantum = SCULL_QUANTUM;
int scull_qset = SCULL_QSET;


module_param(scull_major,int,S_IRUGO);
module_param(scull_minor,int,S_IRUGO);
module_param(scull_nr_devs,int,S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);

MODULE_AUTHOR("Alessandro Rubini, Jonathan Corbet");
MODULE_LICENSE("Dual BSD/GPL");

struct scull_dev *scull_devices;

int scull_trim(struct scull_dev *dev){

	struct scull_qset *next, *dptr;
	int qset = dev->qset;
	int i;

	printk("TRIM\n");
	for(dptr = dev->data; dptr; dptr = next){
		if(dptr->data){
			for(i = 0;i < qset; i++)
				kfree(dptr->data[i]);
			kfree(dptr->data);
			dptr->data = NULL;
		}
		next = dptr->next;
		kfree(dptr);
	}

	dev->size = 0;
	dev->quantum = scull_quantum;
	dev->qset = scull_qset;
	dev->data = NULL;
	return 0;
}
static void *scull_seq_start(struct seq_file *s, loff_t *pos)
{
	if(*pos >= scull_nr_devs)
		return NULL;
	return scull_devices + *pos;
}

static void *scull_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	if(*pos >= scull_nr_devs)
		return NULL;
	return scull_devices + *pos;
}

static void scull_seq_stop(struct seq_file *s, void *v)
{
	/* Actually, there's nothing to do here */
}

static int scull_seq_show(struct seq_file *s,void *v)
{
	struct scull_dev *dev = (struct scull_dev *)v;
	struct scull_qset *d;
	int i;

	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	seq_printf(s, "\nDevice %i: qset %i, q %i, sz %li\n",
 			(int) (dev - scull_devices), dev->qset,
 			dev->quantum, dev->size);
	for(d = dev->data;d;d = d->next){
		seq_printf(s, " item at %p, qset at %p\n", d, d->data);
		if(d->data && !d->next)
			for(i = 0; i < dev->qset; i++){
				if(d->data[i])
					seq_printf(s, "	%4i: %8p\n",i,d->data[i]);
			}
	}
	up(&dev->sem);
	return 0;
}

static struct seq_operations scull_seq_ops = {
 .start = scull_seq_start,
 .next = scull_seq_next,
 .stop = scull_seq_stop,
 .show = scull_seq_show
};

static int scullseq_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &scull_seq_ops);
}

/*
 * Create a set of file operations for our proc files.
 */

static struct file_operations scullseq_proc_ops = {
	.owner   = THIS_MODULE,
	.open    = scullseq_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

static void create_scull_procfs(void)
{
	proc_create("scullseq",0,NULL,&scullseq_proc_ops);
}

static void remove_scull_procfs(void)
{
	remove_proc_entry("scullseq",NULL);
}

static struct scull_qset *scull_follow(struct scull_dev *dev, int n)
{
	struct scull_qset *qs = dev->data;

	/* Allocate first qset explicitly if need be */
	if (! qs) {
		qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
		if (qs == NULL)
			return NULL;  /* Never mind */

		memset(qs, 0, sizeof(struct scull_qset));
	}

	/* Then follow the list */
	while (n--) {
		if (!qs->next) {
			qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
			if (qs->next == NULL)
				return NULL;  /* Never mind */

			memset(qs->next, 0, sizeof(struct scull_qset));
		}
		qs = qs->next;
		continue;
	}
	return qs;
}

ssize_t scull_read(struct file *filp, char __user *buf, size_t count,
					loff_t *f_pos)
{

	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr;
	int row_pos,col_pos,set_num;
	int quantum = dev->quantum, qset = dev->qset; // 4000 & 1000
	ssize_t retval = 0;

	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	printk(KERN_INFO"READ\n");
	PDEBUG("size:%ld\n",dev->size);
	if(*f_pos >= dev->size){
		PDEBUG("File pointer reached end of file\n");
		goto out;
	}

	if (*f_pos + count > dev->size)
		count = dev->size - *f_pos;

	/* set the column ,row and qset positions*/
	row_pos = (long)*f_pos / quantum;
	col_pos = (long)*f_pos % quantum;
	set_num = row_pos / qset;
	PDEBUG("f_pos:%ld count:%d\n",(long)*f_pos,count);
	PDEBUG(KERN_INFO"row_pos: %d col_pos:%d set_row:%d\n",row_pos,col_pos,set_num);

	/* follow the list up to the right position (defined elsewhere) */
 	dptr = scull_follow(dev, set_num);

 	if(dptr == NULL || !dptr->data || !dptr->data[row_pos])
 		goto out;

 	/* read only up to the end of this quantum */
 	if(count > quantum - col_pos)
 		count = quantum - col_pos;


 	if(copy_to_user(buf, dptr->data[row_pos] + col_pos, count)){
 		retval = -EFAULT;
 		goto out;
 	}
 	
 	*f_pos += count;
 	retval = count;
out:
	up(&dev->sem);
 	return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf,size_t count,
					loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr;
	int row_pos,col_pos,set_num;
	int quantum = dev->quantum, qset = dev->qset; // 4000 & 1000
	int max_size = quantum * qset;
	int err = 0;
	ssize_t retval = -ENOMEM;

	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	PDEBUG("size:%ld\n",dev->size);
	if((long)*f_pos > max_size)
		goto out;
	if(dev->size > max_size)
		goto out;

	/* set the column ,row and qset positions*/
	row_pos = (long)*f_pos / quantum;
	col_pos = (long)*f_pos % quantum;
	set_num = (row_pos / qset);
	PDEBUG("f_pos:%ld\n",(long)*f_pos);
	PDEBUG(KERN_INFO"row_pos: %d col_pos:%d set_row:%d\n",row_pos,col_pos,set_num);

	/* follow to the end of the list*/
	dptr = scull_follow(dev, set_num);

	if(!dptr)
		goto out;

	/* allocate qset array of character pointers*/
	if(!dptr->data){
		dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
		if(!dptr->data)
			goto out;

		memset(dptr->data, 0, qset * sizeof(char *));
	}

	/* allocate a quantum for the above allocated row */
	if(!dptr->data[row_pos]){
		dptr->data[row_pos] = kmalloc(quantum * sizeof(char), GFP_KERNEL);
		if(!dptr->data[row_pos])
			goto out;
	}

	/* report how many bytes of data didn't get copied */
	err = copy_from_user(dptr->data[row_pos]+col_pos, buf, count);
	if(err) {
		retval = -EFAULT;
		printk(KERN_INFO"could not write %d bytes\n",err);
		goto out;
	}
	printk(KERN_INFO"WRITE\n");

	/* update filepoineter position and total size */
	*f_pos += count;
	dev->size += count;
	retval = count;

  out:
	up(&dev->sem);
	return retval;
}

int scull_open(struct inode *inode, struct file *filp){
	struct scull_dev *dev;

	printk(KERN_INFO"%s: OPEN\n", __func__);
	/* Get structure pointer by passing a member of the structure which is cdev*/
	dev = container_of(inode->i_cdev, struct scull_dev, cdev);
	filp->private_data = dev;	//To access later store the structure address
	
	/* check for write only mode if so trim the fie to 0bytes*/
	if((filp->f_flags & O_ACCMODE) == O_WRONLY)
		scull_trim(dev);

	return 0;
}

int scull_release(struct inode *inode, struct file *filp){
	printk("RELEASE\n");
	return 0;
}

struct file_operations scull_fops = {	//scull file opertaions
	.owner = THIS_MODULE,
 	//.llseek = NULL,
 	.read = scull_read,
 	.write = scull_write,
 	//.ioctl = NULL,
 	.open = scull_open,
 	.release = scull_release,
};

/*
 * Set up the char_dev structure for this device.
 */
void scull_setup_cdev(struct scull_dev *dev,int index){

	int err,devno = MKDEV(scull_major,scull_minor + index);	//get each device number

	cdev_init(&dev->cdev,&scull_fops);	//init char device
	dev->cdev.owner = THIS_MODULE;		//Set owner of device
	err = cdev_add(&dev->cdev,devno,1);		//add char device and associate one device with it
	if(err < 0)
		printk(KERN_WARNING"%s:failed to add major:%d\n", __func__,MAJOR(devno));
}

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 */
void scull_cleanup_module(void)
{
	int i;
	dev_t devno = MKDEV(scull_major, scull_minor);

	/* Get rid of our char dev entries */
	if (scull_devices) {
		for (i = 0; i < scull_nr_devs; i++) {
			scull_trim(scull_devices + i);
			cdev_del(&scull_devices[i].cdev);
		}
		kfree(scull_devices);
	}

	remove_scull_procfs();

	/* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, scull_nr_devs);
}

/*
 * init scull driver
 */
int scull_init(void){

	int err = 0,i,result;
	dev_t dev;

	printk(KERN_INFO"%s:start scull_init", __func__);
	if(scull_major){
			//Combine major and minor into a 32bit binary number 
			dev = MKDEV(scull_major,scull_minor);
			result = register_chrdev_region(dev,scull_nr_devs,"scull");
	}else {
		err = alloc_chrdev_region(&dev,scull_minor,scull_nr_devs,"scull");
		scull_major = MAJOR(dev);
		scull_minor = MINOR(dev);
		printk(KERN_NOTICE "%s:major:%d and minor:%d\n", __func__,scull_major,scull_minor);
	}
	if(err){
		printk(KERN_WARNING "%s:device cant get major %d\n", __func__,scull_major);
		result = err;
		goto failed;
	}
	scull_devices = kmalloc(scull_nr_devs * sizeof(struct scull_dev),GFP_KERNEL);
	if(!scull_devices){
		result = -ENOMEM;
		goto failed;
	}
	memset(scull_devices,0,scull_nr_devs * sizeof(struct scull_dev));
	for(i = 0; i<=scull_nr_devs;i++){
		scull_devices[i].quantum = scull_quantum;
		scull_devices[i].qset = scull_qset;
		sema_init(&scull_devices[i].sem, 1);
		scull_setup_cdev(&scull_devices[i],i);
	}

	create_scull_procfs();

	return 0;
failed:
	pr_err(KERN_INFO"%s: Scull device setup failed\n", __func__);
	remove_scull_procfs();
	scull_cleanup_module();
	return result;
}

module_init(scull_init);
module_exit(scull_cleanup_module);
