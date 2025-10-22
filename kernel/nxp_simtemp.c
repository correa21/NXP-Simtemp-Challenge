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

#ifndef no_llseek
#define no_llseek noop_llseek
#endif

#define DRIVER_NAME "nxp_simtemp"
#define DEVICE_NAME "simtemp"

#define FIFO_SIZE   256 // Must be a power of two for kfifo

#define SAMPLING_MS 1000
#define TEMP_THRESHOLD 42000

/* Configuration bounds */
#define MIN_SAMPLING_MS 10
#define MAX_SAMPLING_MS 60000

/* Enum for simulation modes */
enum simtemp_mode {
	MODE_NORMAL,
	MODE_NOISY,
	MODE_RAMP,
	MODE_MAX,
};

struct simtemp_device {

   	struct platform_device *pdev;
    struct miscdevice miscdev;
    struct hrtimer timer;

	spinlock_t lock; /* Protects kfifo and event_flags */
    wait_queue_head_t wq;


   	/* Configuration */
	u32 sampling_ms;
	s32 threshold_mC;
	enum simtemp_mode mode;

	/* State & Stats */
	s32 ramp_temp;
	u32 event_flags;
	u64 update_count;
	u64 alert_count;
	int last_error;

	/* The kfifo to buffer samples */
    DECLARE_KFIFO(sample_fifo, struct simtemp_sample, FIFO_SIZE);
};

/* --- Temperature Simulation --- */

static s32 generate_temp(struct simtemp_device *sdev)
{
	s32 base_temp = 42000; /* 42.000 °C */
	s32 temp;
	s32 jitter = (get_random_u32() % 2000) - 1000; /* +/- 1.0 °C */

	switch (sdev->mode) {
		case MODE_NOISY:
			jitter *= 5; /* More noise */
			/* fallthrough */
			fallthrough; // kernel's dedicated fallthrough macro
		case MODE_NORMAL:
			temp = base_temp + jitter;
			break;
		case MODE_RAMP:
			sdev->ramp_temp += 500; /* Ramp up by 0.5 °C each sample */
			if (sdev->ramp_temp > 85000){
				sdev->ramp_temp = 25000; /* Reset ramp */
			}
			temp = sdev->ramp_temp + (jitter / 2);
			break;
		default:
			temp = 0;
		}

	return temp;
}

static enum hrtimer_restart simtemp_timer_callback(struct hrtimer *timer)
{
    unsigned long flags;

    struct simtemp_device *sdev = container_of(timer, struct simtemp_device, timer);
    struct simtemp_sample sample;

	sample.timestamp_ns = ktime_get_real_ns();
	sample.temp_mC = generate_temp(sdev);
	sample.flags = SIMTEMP_FLAG_NEW_SAMPLE;

	spin_lock_irqsave(&sdev->lock, flags);

	sdev->update_count++;
	if (sample.temp_mC >= sdev->threshold_mC) {
		sample.flags |= SIMTEMP_FLAG_THRESHOLD_CROSSED;
		sdev->event_flags |= SIMTEMP_FLAG_THRESHOLD_CROSSED;
		sdev->alert_count++;
	}

    pr_debug("nxp_simtemp - timestamp: %llu, temp: %i, flags: %i\n", sample.timestamp_ns, sample.temp_mC, sample.flags);

    kfifo_put(&sdev->sample_fifo, sample);

    /* Reschedule the timer */
	hrtimer_forward_now(timer, ms_to_ktime(sdev->sampling_ms));

	spin_unlock_irqrestore(&sdev->lock, flags);

	/* Wake up any processes waiting for data or events */
	wake_up_interruptible(&sdev->wq);



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
	unsigned long flags;

	if (ret){
		return ret;
	}
	if (new_period < MIN_SAMPLING_MS || new_period > MAX_SAMPLING_MS){ /* Sanity check */
		return -EINVAL;
	}

	spin_lock_irqsave(&sdev->lock, flags);
	sdev->sampling_ms = new_period;
	hrtimer_start(&sdev->timer, ms_to_ktime(sdev->sampling_ms), HRTIMER_MODE_REL);
	spin_unlock_irqrestore(&sdev->lock, flags);
	pr_debug("nxp_simtemp - new_sampling: %u\n", sdev->sampling_ms);

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
	unsigned long flags;

	if (ret){
		return ret;
	}
	spin_lock_irqsave(&sdev->lock, flags);
	sdev->threshold_mC = new_thresh;
	spin_unlock_irqrestore(&sdev->lock, flags);
	pr_debug("nxp_simtemp - new_threshold: %i\n", sdev->threshold_mC);

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

	spin_lock_irqsave(&sdev->lock, flags);
	updates = sdev->update_count;
	alerts = sdev->alert_count;
	err = sdev->last_error;
	spin_unlock_irqrestore(&sdev->lock, flags);

	return sysfs_emit(buf, "updates=%llu alerts=%llu last_error=%d\n", updates, alerts, err);
}
static DEVICE_ATTR_RO(stats);

/* mode (RW) */
static const char *const simtemp_mode_str[] = { "normal", "noisy", "ramp" };
static ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct simtemp_device *sdev = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%s\n", simtemp_mode_str[sdev->mode]);
}
static ssize_t mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct simtemp_device *sdev = dev_get_drvdata(dev);
	int i;
	unsigned long flags;

	for (i = 0; i < MODE_MAX; i++) {
		if (sysfs_streq(buf, simtemp_mode_str[i])) {
			spin_lock_irqsave(&sdev->lock, flags);
			sdev->mode = i;
			if (i == MODE_RAMP){
				sdev->ramp_temp = 25000; /* Reset ramp on mode set */
			}
			spin_unlock_irqrestore(&sdev->lock, flags);
			return count;
		}
	}
	return -EINVAL;
}
static DEVICE_ATTR_RW(mode);

static struct attribute *simtemp_attrs[] = {
	&dev_attr_sampling_ms.attr,
	&dev_attr_threshold_mC.attr,
	&dev_attr_stats.attr,
	&dev_attr_mode.attr,
	NULL,
};
ATTRIBUTE_GROUPS(simtemp);

/* --- File Operations --- */
static int simtemp_open (struct inode *inode, struct file *file)
{
    /* Retrieve the miscdevice, then our private data from it */
	struct miscdevice *miscdev = file->private_data;
	struct simtemp_device *sdev = container_of(miscdev, struct simtemp_device, miscdev);

	file->private_data = sdev;

    return 0;
}
static int simtemp_release (struct inode *inode, struct file *file)
{
    pr_info("nxp_simtemp - file released");
    return 0;
}
static ssize_t simtemp_read(struct file *file, char __user *user_buf, size_t len, loff_t *off)
{
   	struct simtemp_device *sdev = file->private_data;
    int ret;
    struct simtemp_sample sample;
    size_t sample_size = sizeof(struct simtemp_sample);
   	unsigned long flags;

    /* We only support reading whole samples at a time */
    if (len < sample_size){
        return -EINVAL;
    }

	if (kfifo_is_empty(&sdev->sample_fifo)) {
		if (file->f_flags & O_NONBLOCK){
			return -EAGAIN;
		}
		/* Blocking read wait condition */
		ret = wait_event_interruptible(sdev->wq, !kfifo_is_empty(&sdev->sample_fifo));
		if (ret){
			return ret; /* Interrupted by a signal */
		}
	}

    printk("nxp_simtemp - Read is called");

    spin_lock_irqsave(&sdev->lock, flags);

	ret = kfifo_get(&sdev->sample_fifo, &sample);

	if (!ret) { /* Should not happen due to wait_event, but better be safe */
		spin_unlock_irqrestore(&sdev->lock, flags);
		return -EAGAIN;
	}

    /* Clear event flags on read */
	sdev->event_flags = 0;
	spin_unlock_irqrestore(&sdev->lock, flags);
	if (copy_to_user(user_buf, &sample, sizeof(sample))) {
    	spin_lock_irqsave(&sdev->lock, flags);
        sdev->last_error = -EFAULT;
        spin_unlock_irqrestore(&sdev->lock, flags);
	    return -EFAULT;
	}
	return sizeof(sample);
}
static long simtemp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    pr_debug("nxp_simtemp - Received ioctl command: %#x\n", cmd);
    pr_debug("nxp_simtemp - Expected ioctl command: %#lx\n", (long unsigned int)SIMTEMP_IOC_SET_CONFIG);
    struct simtemp_device *sdev = file->private_data;
	struct simtemp_config config;
	unsigned long flags;

	if (cmd == SIMTEMP_IOC_SET_CONFIG) {
		if (copy_from_user(&config, (void __user *)arg, sizeof(config))){
			return -EFAULT;
		}

		if (config.sampling_ms < MIN_SAMPLING_MS || config.sampling_ms > MAX_SAMPLING_MS){
			return -EINVAL;
		}

		spin_lock_irqsave(&sdev->lock, flags);
		sdev->sampling_ms = config.sampling_ms;
		sdev->threshold_mC = config.threshold_mC;
		spin_unlock_irqrestore(&sdev->lock, flags);

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

	spin_lock_irqsave(&sdev->lock, flags);
	if (!kfifo_is_empty(&sdev->sample_fifo)){
		mask |= EPOLLIN | EPOLLRDNORM; /* Data available to read */
	}

	if (sdev->event_flags & SIMTEMP_FLAG_THRESHOLD_CROSSED){
		mask |= EPOLLPRI; /* High-priority event (alert) */
	}
	spin_unlock_irqrestore(&sdev->lock, flags);

	return mask;
}

static struct file_operations simtemp_fops = {
    .owner = THIS_MODULE,
    .open = simtemp_open,
    .release = simtemp_release,
    .read = simtemp_read,
    .poll = simtemp_poll,
    .unlocked_ioctl = simtemp_ioctl,
    .llseek = no_llseek
};


/* --- Platform Driver --- */

static const struct of_device_id simtemp_of_match[] = {
	{ .compatible = "nxp,simtemp" },
	{ }
};
MODULE_DEVICE_TABLE(of, simtemp_of_match);

static int simtemp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct simtemp_device *sdev;
	int ret;

	sdev = devm_kzalloc(dev, sizeof(*sdev), GFP_KERNEL);
	if (!sdev){
	    return -ENOMEM;
	}

	platform_set_drvdata(pdev, sdev);
	sdev->pdev = pdev;

	/* Initialize private data */
	spin_lock_init(&sdev->lock);
	init_waitqueue_head(&sdev->wq);
	INIT_KFIFO(sdev->sample_fifo);
    sdev->ramp_temp = 25000;

	/* Parse Device Tree properties or use defaults */
	sdev->sampling_ms = 1000; /* Default: 1 second */
	sdev->threshold_mC = 50000; /* Default: 50.0 °C */
	/* Only read DT if a node exists. */
    if (dev->of_node) {
	    of_property_read_u32(dev->of_node, "sampling-ms", &sdev->sampling_ms);
	    of_property_read_s32(dev->of_node, "threshold-mC", &sdev->threshold_mC);
    }

	/* Set up and register the misc character device */
	sdev->miscdev.minor = MISC_DYNAMIC_MINOR;
	sdev->miscdev.name = DEVICE_NAME;
	sdev->miscdev.fops = &simtemp_fops;
	sdev->miscdev.groups = simtemp_groups;
	ret = misc_register(&sdev->miscdev);
	if (ret) {
		dev_err(dev, "Failed to register misc device\n");
		return ret;
	}

	dev_set_drvdata(sdev->miscdev.this_device, sdev);

	/* Initialize and start the high-resolution timer */
	hrtimer_init(&sdev->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	sdev->timer.function = &simtemp_timer_callback;
	hrtimer_start(&sdev->timer, ms_to_ktime(sdev->sampling_ms), HRTIMER_MODE_REL);

	dev_info(dev, "NXP Virtual Temperature Sensor initialized\n");

	return 0;
}

#ifdef CONFIG_64BIT
// For 64-bit systems, use the expected return type int
static int simtemp_remove(struct platform_device *pdev)
#else
// For 32-bit systems, use the return type void
static void simtemp_remove(struct platform_device *pdev)
#endif
{
	struct simtemp_device *sdev = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "Unloading NXP Virtual Temperature Sensor\n");

	hrtimer_cancel(&sdev->timer);
	misc_deregister(&sdev->miscdev);
#ifdef CONFIG_64BIT
    return 0; // Only return 0 when compiling for 64-bit
#endif
}


static struct platform_driver simtemp_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = simtemp_of_match,
	},
	.probe = simtemp_probe,
	.remove = simtemp_remove,
};

#ifdef DEBUG
static struct platform_device *simtemp_pdev;

static int __init simtemp_module_init(void)
{
    int ret;
    pr_info("nxp_simtemp: module init\n");

    // Manually create a platform device. This is what the Device Tree
    // would normally do for us at boot time.
    simtemp_pdev = platform_device_alloc(DRIVER_NAME, -1);
    if (!simtemp_pdev) {
        pr_err("nxp_simtemp: Failed to allocate platform device\n");
        return -ENOMEM;
    }

    ret = platform_device_add(simtemp_pdev);
    if (ret) {
        pr_err("nxp_simtemp: Failed to add platform device\n");
        platform_device_put(simtemp_pdev); // Cleanup on failure
        return ret;
    }

    // Now, register our platform driver. The kernel will see that our new
    // device's name matches our driver's name and will call our probe() function.
    ret = platform_driver_register(&simtemp_driver);
    if (ret) {
        pr_err("nxp_simtemp: Failed to register platform driver\n");
        platform_device_unregister(simtemp_pdev); // Cleanup on failure
        return ret;
    }

    return 0;
}

static void __exit simtemp_module_exit(void)
{
    pr_info("nxp_simtemp: module exit\n");

    // Unregister in the reverse order of registration
    platform_driver_unregister(&simtemp_driver);
    platform_device_unregister(simtemp_pdev);
}

module_init(simtemp_module_init);
module_exit(simtemp_module_exit);
#else
module_platform_driver(simtemp_driver);
#endif
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Armando Correa");
MODULE_DESCRIPTION("A sample driver for simulating a temperature sensor");
MODULE_VERSION("1.0");
