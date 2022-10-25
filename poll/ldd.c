#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/poll.h>

#define MAX_LEN 10
dev_t dev = 0;

struct ldd_buf{
	wait_queue_head_t readq, writeq; /* wait queue head for reader and writers */
	char *buffer;
	int write_idx, read_idx;
	struct cdev c_dev;	/* char device struct */
	struct fasync_struct *async_queue; /* asynchronous readers */
};

static struct ldd_buf *ldd_dev;

int ldd_open(struct inode *inode, struct file *filp)
{
	printk("\nOpen file\n");
	return 0;	
}

ssize_t ldd_read(struct file *filp, char __user *buf, size_t count, loff_t *offset)
{
	int retval = 0;
	//Wait on queue if no data in buffer
	printk("\n%s:read:%d write:%d\n",__func__,ldd_dev->read_idx, ldd_dev->write_idx);
	
	while(ldd_dev->write_idx == ldd_dev->read_idx){
		printk("Nothing to read\n");
		if (filp->f_flags & O_NONBLOCK)
 			return -EAGAIN;
 		printk("\"%s\" reading: going to sleep\n", current->comm);
 		if (wait_event_interruptible(ldd_dev->readq, (ldd_dev->write_idx == ldd_dev->read_idx)))
			return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
	}
	
	//Wrap around if reached end
	if(ldd_dev->read_idx == MAX_LEN)
		ldd_dev->read_idx = 0;
	//counts bytes until either end of buffer or till write index
	count = ldd_dev->write_idx > ldd_dev->read_idx ? (
			ldd_dev->write_idx - ldd_dev->read_idx) : (MAX_LEN - ldd_dev->read_idx);
			
	if(copy_to_user(buf,(ldd_dev->buffer + ldd_dev->read_idx),count)){
		retval = -EFAULT;
		goto out;	
	}
	printk("%s:read:%s\n",__func__,buf);
	ldd_dev->read_idx +=count;
	printk("read index:%d\n",ldd_dev->read_idx);
	//wake up sleeping writers if any
	wake_up_interruptible(&ldd_dev->writeq);
	return 0;
out:
	return retval;
}

static int get_free_space(void)
{
	int count;
	
	if(ldd_dev->write_idx == (ldd_dev->read_idx - 1))
		goto out;
	if(MAX_LEN == (ldd_dev->write_idx - ldd_dev->read_idx))
		goto out;
		
	//Wrap around if reached end
	if(MAX_LEN == ldd_dev->write_idx)
		ldd_dev->write_idx = 0;
	count = ldd_dev->write_idx >= ldd_dev->read_idx ? 
		(MAX_LEN - ldd_dev->write_idx) : (ldd_dev->read_idx - ldd_dev->write_idx - 1);
	
	return count;
out:
	printk("no space left\n");
		return 0;
}

ssize_t ldd_write(struct file *filp, const char __user *buf, size_t count, loff_t *offset)
{
	int retval = 0, tmp, err;
	//wait on queue if no space left on buffer
	printk("\n%s:read:%d write:%d\n",__func__,ldd_dev->read_idx,ldd_dev->write_idx);
	
	while(!get_free_space()){
		DEFINE_WAIT(ldd_wait);
		printk("No space to write\n");
		if (filp->f_flags & O_NONBLOCK)
 			return -EAGAIN;
 		printk("\"%s\" writing: going to sleep\n", current->comm);
 		prepare_to_wait(&ldd_dev->writeq, &ldd_wait, TASK_INTERRUPTIBLE);
		if (!get_free_space())
			schedule();
		finish_wait(&ldd_dev->writeq, &ldd_wait);
		if (signal_pending(current))
			return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
	}
	
	//Get how much space is left in buffer
	tmp = get_free_space();
	//We don't have enughg space in buffer to complete in one operation
	if(count > tmp)
		count = tmp;
	err = copy_from_user((ldd_dev->buffer + ldd_dev->write_idx), buf, count);
	if(err) {
		retval = -EFAULT;
		printk(KERN_INFO"could not write %d bytes\n",err);
		goto out;
	}
	ldd_dev->write_idx += count;
	printk("write index:%d\n",ldd_dev->write_idx);
	
	//Wakeup sleeping readers if any
	wake_up_interruptible(&ldd_dev->readq);
	
	/* and signal asynchronous readers, explained late in chapter 5 */
	if (ldd_dev->async_queue)
		kill_fasync(&ldd_dev->async_queue, SIGIO, POLL_IN);
		
	return count;
out:
	return retval;
}

static int ldd_fasync(int fd, struct file *filp, int mode)
{
	return fasync_helper(fd, filp, mode, &ldd_dev->async_queue);
}

int ldd_close(struct inode *inode, struct file *filp){
	/* remove this filp from the asynchronously notified filp's */
	ldd_fasync(-1, filp, 0);
	printk("close file\n");
	return 0;
}

static unsigned int ldd_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;
	
	poll_wait(filp, &ldd_dev->writeq, wait);
	poll_wait(filp, &ldd_dev->readq, wait);
	if(ldd_dev->write_idx == ldd_dev->read_idx)
		mask |= POLLIN | POLLRDNORM;
	if(get_free_space())
		mask |= POLLOUT | POLLWRNORM;
	
	return mask;
}

struct file_operations ldd_op = {
	.owner = THIS_MODULE,
	.open = ldd_open,
	.read = ldd_read,
	.write = ldd_write,
	.release = ldd_close,
	.poll = ldd_poll,
	.fasync = ldd_fasync,
};

static int ldd_buf_init(void)
{
	int retval = 0;
	ldd_dev->buffer = kmalloc(10 * sizeof(char),GFP_KERNEL);
	if(!ldd_dev->buffer){
		printk("Memory allocationg failed\n");
		retval = -ENOMEM;
		goto out;
	}
	init_waitqueue_head(&ldd_dev->readq);
	init_waitqueue_head(&ldd_dev->writeq);
	ldd_dev->write_idx = ldd_dev->read_idx =  0;
	return 0;
out:
	return retval;
}

int ldd_init(void)
{
	int retval = 0, err= 0;
	dev = MKDEV(240,0);
	if((register_chrdev_region(dev,1,"ldd")) < 0){
		printk("Failed to alloc chardev region\n");
		return -1;	
	}
	ldd_dev = kmalloc(sizeof(struct ldd_buf), GFP_KERNEL);
	if(!ldd_dev)
		goto end;
	cdev_init(&ldd_dev->c_dev,&ldd_op);
	err = cdev_add(&ldd_dev->c_dev,dev,1);
	if( err < 0){
		printk("Failed to add chardev\n");
		goto out;	
	}
	if(ldd_buf_init()){
		printk("Failed to initilize essential structure\n");
		goto out;
	}
	printk("Init done\n");
	return 0;
out:
	cdev_del(&ldd_dev->c_dev);
	unregister_chrdev_region(dev,1);
	return retval;
end:
	return -ENOMEM;
}

void ldd_exit(void){
	cdev_del(&ldd_dev->c_dev);
	unregister_chrdev_region(dev,1);
}

MODULE_LICENSE("Dual BSD/GPL");
module_init(ldd_init);
module_exit(ldd_exit);
