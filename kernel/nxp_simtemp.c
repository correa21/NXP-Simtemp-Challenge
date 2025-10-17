#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/hrtimer.h>
#include <linux/kfifo.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/poll.h>

#include "nxp_simtemp.h"
#include "nxp_simtemp_ioctl.h"


#define DRIVER_NAME "nxp_simtemp"
#define DEVICE_NAME "simtemp"

#define FIFO_SIZE   256 // Must be a power of two for kfifo

#define SAMPLING_MS 1000
#define TEMP_THRESHOLD 42000

/* Configuration bounds */
#define MIN_SAMPLING_MS 10
#define MAX_SAMPLING_MS 60000


struct simtemp_device {

    struct miscdevice miscdev;
    struct hrtimer timer;
	spinlock_t fifo_lock; /* Protects kfifo and event_flags */
    wait_queue_head_t wq;
   	struct mutex lock; /* Protects config variables */


   	/* Configuration */
	u32 sampling_ms;
	s32 threshold_mC;

	/* State & Stats */
	u32 event_flags;
	u64 update_count;
	u64 alert_count;
	int last_error;

	/* The kfifo to buffer samples */
    DECLARE_KFIFO(sample_fifo, struct simtemp_sample, FIFO_SIZE);
};

static struct simtemp_device *simtemp_device;

/* --- Temperature Simulation --- */

static s32 generate_temp(void)
{
	static s32 base_temp = 42000; /* 42.000 °C */
	s32 temp;

    temp = base_temp + (get_random_u32() % 200) - 100; /* +/- 0.1 °C jitter */

	return temp;
}

static enum hrtimer_restart simtemp_timer_callback(struct hrtimer *timer)
{
    unsigned long flags;

    struct simtemp_device *sdev = container_of(timer, struct simtemp_device, timer);
    struct simtemp_sample sample;

	sample.timestamp_ns = ktime_get_real_ns();
	sample.temp_mC = generate_temp();
	sample.flags = SIMTEMP_FLAG_NEW_SAMPLE;

	spin_lock_irqsave(&sdev->fifo_lock, flags);

	sdev->update_count++;
	if (sample.temp_mC >= sdev->threshold_mC) {
		sample.flags |= SIMTEMP_FLAG_THRESHOLD_CROSSED;
		sdev->event_flags |= SIMTEMP_FLAG_THRESHOLD_CROSSED;
		sdev->alert_count++;
	}

    pr_debug("nxp_simtemp - timestamp: %llu, temp: %i\n, flags: %i", sample.timestamp_ns, sample.temp_mC, sample.flags);

    kfifo_put(&sdev->sample_fifo, sample);


	spin_unlock_irqrestore(&sdev->fifo_lock, flags);

	/* Wake up any processes waiting for data or events */
	wake_up_interruptible(&sdev->wq);

    /* Reschedule the timer */
	hrtimer_forward_now(timer, ms_to_ktime(sdev->sampling_ms));

	return HRTIMER_RESTART;
}

/* --- Sysfs Attributes --- */

/* sampling_ms (RW) */
static ssize_t sampling_ms_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct simtemp_device *sdev = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%u\n", sdev->sampling_ms);
}
static ssize_t sampling_ms_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct simtemp_device *sdev = dev_get_drvdata(dev);
	u32 new_period;
	int ret = kstrtou32(buf, 10, &new_period);

	if (ret)
		return ret;
	if (new_period < 10 || new_period > 60000) /* Sanity check: 10ms to 60s */
		return -EINVAL;

	mutex_lock(&sdev->lock);
	sdev->sampling_ms = new_period;
	hrtimer_start(&sdev->timer, ms_to_ktime(sdev->sampling_ms), HRTIMER_MODE_REL);
	mutex_unlock(&sdev->lock);

	return count;
}
static DEVICE_ATTR_RW(sampling_ms);

/* threshold_mC (RW) */
static ssize_t threshold_mC_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct simtemp_device *sdev = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%d\n", sdev->threshold_mC);
}
static ssize_t threshold_mC_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct simtemp_device *sdev = dev_get_drvdata(dev);
	s32 new_thresh;
	int ret = kstrtos32(buf, 10, &new_thresh);

	if (ret)
		return ret;

	mutex_lock(&sdev->lock);
	sdev->threshold_mC = new_thresh;
	mutex_unlock(&sdev->lock);

	return count;
}
static DEVICE_ATTR_RW(threshold_mC);

/* stats (RO) */
static ssize_t stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct simtemp_device *sdev = dev_get_drvdata(dev);
	unsigned long flags;
	u64 updates, alerts;
	int err;

	spin_lock_irqsave(&sdev->fifo_lock, flags);
	updates = sdev->update_count;
	alerts = sdev->alert_count;
	spin_unlock_irqrestore(&sdev->fifo_lock, flags);
	mutex_lock(&sdev->lock);
	err = sdev->last_error;
	mutex_unlock(&sdev->lock);

	return sysfs_emit(buf, "updates=%llu alerts=%llu last_error=%d\n", updates, alerts, err);
}
static DEVICE_ATTR_RO(stats);

static struct attribute *simtemp_attrs[] = {
	&dev_attr_sampling_ms.attr,
	&dev_attr_threshold_mC.attr,
	&dev_attr_stats.attr,
	NULL,
};
ATTRIBUTE_GROUPS(simtemp);

/* --- File Operations --- */
static int my_open (struct inode *inode, struct file *file)
{
    pr_info("nxp_simtemp - Major: %d, Minor %d\n", imajor(inode), iminor(inode));
    pr_info("nxp_simtemp - file->f_pos: %lld\n", file->f_pos);
    pr_info("nxp_simtemp - file->f_mode: 0x%x\n", file->f_mode);
    pr_info("nxp_simtemp - file->f_flags: 0x%x\n", file->f_flags);

    return 0;
}
static int my_release (struct inode *inode, struct file *file)
{
    pr_info("nxp_simtemp - file released");
    return 0;
}
static ssize_t my_read(struct file *file, char __user *user_buf, size_t len, loff_t *off)
{
   	struct simtemp_device *sdev = file->private_data;
    int ret;
    struct simtemp_sample sample;
    size_t sample_size = sizeof(struct simtemp_sample);
   	unsigned long flags;

    /* We only support reading whole samples at a time */
    if (len < sample_size)
        return -EINVAL;

    /* Blocking read wait condition */
	ret = wait_event_interruptible(sdev->wq, !kfifo_is_empty(&sdev->sample_fifo));
	if (ret)
		return ret;

    printk("nxp_simtemp - Read is called");

    spin_lock_irqsave(&sdev->fifo_lock, flags);

	ret = kfifo_get(&sdev->sample_fifo, &sample);

	if (!ret) { /* Should not happen due to wait_event, but better be safe */
		spin_unlock_irqrestore(&sdev->fifo_lock, flags);
		return -EAGAIN;
	}

    /* Clear event flags on read */
	sdev->event_flags = 0;
	spin_unlock_irqrestore(&sdev->fifo_lock, flags);
	if (copy_to_user(user_buf, &sample, sizeof(sample))) {
    	mutex_lock(&sdev->lock);
        sdev->last_error = -EFAULT;
        mutex_unlock(&sdev->lock);
	    return -EFAULT;
	}
	return sizeof(sample);
}
static long my_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    pr_debug("nxp_simtemp - Received ioctl command: %#x\n", cmd);
    pr_debug("nxp_simtemp - Expected ioctl command: %#lx\n", SIMTEMP_IOC_SET_CONFIG);
    struct simtemp_device *sdev = file->private_data;
	struct simtemp_config config;

	if (cmd == SIMTEMP_IOC_SET_CONFIG) {
		if (copy_from_user(&config, (void __user *)arg, sizeof(config)))
			return -EFAULT;

		if (config.sampling_ms < MIN_SAMPLING_MS || config.sampling_ms > MAX_SAMPLING_MS)
			return -EINVAL;

		mutex_lock(&sdev->lock);
		sdev->sampling_ms = config.sampling_ms;
		sdev->threshold_mC = config.threshold_mC;
		mutex_unlock(&sdev->lock);

		 pr_info("nxp_simtemp - Config changed, sampling: %u, threshold: %i\n", sdev->sampling_ms, sdev->threshold_mC);

		/* Restart timer with new period */
		hrtimer_start(&sdev->timer, ms_to_ktime(sdev->sampling_ms), HRTIMER_MODE_REL);
		return 0;
	}

	return -ENOTTY;
}
static __poll_t simtemp_poll(struct file *file, struct poll_table_struct *wait)
{
	struct simtemp_device *sdev = file->private_data;
	__poll_t mask = 0;
	unsigned long flags;

	poll_wait(file, &sdev->wq, wait);

	spin_lock_irqsave(&sdev->fifo_lock, flags);
	if (!kfifo_is_empty(&sdev->sample_fifo))
		mask |= EPOLLIN | EPOLLRDNORM; /* Data available to read */
	if (sdev->event_flags & SIMTEMP_FLAG_THRESHOLD_CROSSED)
		mask |= EPOLLPRI; /* High-priority event (alert) */
	spin_unlock_irqrestore(&sdev->fifo_lock, flags);

	return mask;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = my_open,
    .release = my_release,
    .read = my_read,
    .poll = simtemp_poll,
    .unlocked_ioctl = my_ioctl,
    .llseek = no_llseek
};

static struct miscdevice nxp_simtemp = {
    .name = DEVICE_NAME,
    .minor = MISC_DYNAMIC_MINOR,
    .fops = &fops,
    .groups = simtemp_groups
};

static int __init my_init(void)
{

    int status;
    simtemp_device = kzalloc(sizeof(*simtemp_device), GFP_KERNEL);
    if (!simtemp_device){
        return -ENOMEM;
    }

    spin_lock_init(&simtemp_device->fifo_lock);
    init_waitqueue_head(&simtemp_device->wq);
    INIT_KFIFO(simtemp_device->sample_fifo);
    mutex_init(&simtemp_device->lock);

    simtemp_device->sampling_ms = 1000; /* Default: 1 second */
    simtemp_device->threshold_mC = 50000; /* Default: 50.0 °C */


    simtemp_device->miscdev = nxp_simtemp;

    pr_info("nxp_simtemp - Register misc device with values: sampling: %u, threshold: %i\n", simtemp_device->sampling_ms, simtemp_device->threshold_mC);
    status = misc_register(&simtemp_device->miscdev);
    if (status) {
        pr_err("nxp_simtemp - Error during Register misc device\n");
        return -status;
    }
    /* Initialize and start the high-resolution timer */
	hrtimer_init(&simtemp_device->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	simtemp_device->timer.function = &simtemp_timer_callback;
	hrtimer_start(&simtemp_device->timer, ms_to_ktime(simtemp_device->sampling_ms), HRTIMER_MODE_REL);
    return 0;
}

static void __exit my_exit(void)
{
    hrtimer_cancel(&simtemp_device->timer);
    misc_deregister(&simtemp_device->miscdev);
    kfree(simtemp_device);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Armando Correa");
MODULE_DESCRIPTION("A sample driver for registering a character device");
