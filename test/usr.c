#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/sched.h>
static struct hrtimer htimer;
static ktime_t kt_periode;
static enum hrtimer_restart timer_function(struct hrtimer * timer)
{
        // @Do your work here. 
        asm ("int $50");
        hrtimer_forward_now(timer, kt_periode);
        return HRTIMER_RESTART;
}
static int __init hi(void)
{
        printk(KERN_INFO "my trigger module is initialized \n");

        kt_periode = ktime_set(1, 0); //seconds,nanoseconds
        hrtimer_init (& htimer, CLOCK_REALTIME, HRTIMER_MODE_REL);
        htimer.function = timer_function;
        hrtimer_start(& htimer, kt_periode, HRTIMER_MODE_REL);
        return 0;
}
static void __exit bye(void)
{
        hrtimer_cancel(& htimer);
        printk(KERN_INFO "my trigger module is distroyed \n");
}
module_init(hi);
module_exit(bye);
MODULE_LICENSE("GPL");
