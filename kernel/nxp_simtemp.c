#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include "nxp_simtemp.h"
#include "nxp_simtemp_ioctl.h"

#define DRIVER_NAME "nxp_simtemp"
#define DEVICE_NAME "simtemp"


#define SAMPLING_MS 1000
#define TEMP_THRESHOLD 42000

/* Configuration bounds */
#define MIN_SAMPLING_MS 10
#define MAX_SAMPLING_MS 60000

static char text[64];
static struct hrtimer timer;
static u32 sampling_ms;
static s32 threshold_mC;
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
	struct simtemp_sample sample;

	sample.timestamp_ns = ktime_get_ns();
	sample.temp_mC = generate_temp();
	sample.flags = SIMTEMP_FLAG_NEW_SAMPLE;


	if (sample.temp_mC >= TEMP_THRESHOLD) {
		sample.flags |= SIMTEMP_FLAG_THRESHOLD_CROSSED;
	}
    
    pr_info("nxp_simtemp - timestamp: %llu, temp: %i\n", sample.timestamp_ns, sample.temp_mC);
	
    /* Reschedule the timer */
	hrtimer_forward_now(timer, ms_to_ktime(sampling_ms));

	return HRTIMER_RESTART;
}

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
static ssize_t my_read(struct file *f, char __user *user_buf, size_t len, loff_t *off)
{
    int not_copied, delta, to_copy = (len + *off) < sizeof(text) ? len :(sizeof(text) - *off);
    struct simtemp_sample sample;
    size_t sample_size = sizeof(struct simtemp_sample);

    if (len < sample_size)
        return -EINVAL;

    printk("nxp_simtemp - Read is called");

	sample.timestamp_ns = ktime_get_real_ns();
	sample.temp_mC = generate_temp();
	sample.flags = SIMTEMP_FLAG_NEW_SAMPLE;

    printk("nxp_simtemp - timestamp: %llu, temp: %i\n", sample.timestamp_ns, sample.temp_mC);

	if (sample.temp_mC >= TEMP_THRESHOLD) {
		sample.flags |= SIMTEMP_FLAG_THRESHOLD_CROSSED;
	}

    if (*off >= sizeof(text))
    {
        return 0;
    }
    not_copied = copy_to_user(user_buf, &sample, sample_size);
    delta = to_copy - not_copied;
    if (not_copied)
    {
        pr_warn("nxp_simtemp - could only copy %d bytes\n", delta);
    }
    *off += delta;
    return delta;
}
static ssize_t my_write(struct file *f, const char __user *user_buf, size_t len, loff_t *off)
{
    int not_copied, delta, to_copy = (len + *off) < sizeof(text) ? len :(sizeof(text) - *off);
    printk("nxp_simtemp - Write is called");

    if (*off >= sizeof(text))
    {
        return 0;
    }
    not_copied = copy_from_user(&text[*off], user_buf, to_copy);
    delta = to_copy - not_copied;
    if (not_copied)
    {
        pr_warn("nxp_simtemp - could only copy %d bytes\n", delta);
    }
    *off += delta;
    return delta;
}
static long my_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    pr_debug("nxp_simtemp - Received ioctl command: %#x\n", cmd);
    pr_debug("nxp_simtemp - Expected ioctl command: %#lx\n", SIMTEMP_IOC_SET_CONFIG);
	struct simtemp_config config;

	if (cmd == SIMTEMP_IOC_SET_CONFIG) {
		if (copy_from_user(&config, (void __user *)arg, sizeof(config)))
			return -EFAULT;

		if (config.sampling_ms < MIN_SAMPLING_MS || config.sampling_ms > MAX_SAMPLING_MS)
			return -EINVAL;


		sampling_ms = config.sampling_ms;
		threshold_mC = config.threshold_mC;

        pr_info("nxp_simtemp - Config changed, sampling: %u, threshold: %i\n", sampling_ms, threshold_mC);

		/* Restart timer with new period */
		hrtimer_start(&timer, ms_to_ktime(sampling_ms), HRTIMER_MODE_REL);
		return 0;
	}

	return -ENOTTY;
}
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = my_open,
    .release = my_release,
    .read = my_read,
    .write = my_write,
    .unlocked_ioctl = my_ioctl
};

static struct miscdevice nxp_simtemp = {
    .name = DEVICE_NAME,
    .minor = MISC_DYNAMIC_MINOR,
    .fops = &fops
};

static int __init my_init(void)
{
    sampling_ms = 1000; /* Default: 1 second */
    threshold_mC = 50000; /* Default: 50.0 °C */
  int status;
  printk("nxp_simtemp - Register misc device\n");
  status = misc_register(&nxp_simtemp);
  if (status) {
    pr_err("nxp_simtemp - Error during Register misc device\n");
    return -status;
  }
  /* Initialize and start the high-resolution timer */
	hrtimer_init(&timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	timer.function = &simtemp_timer_callback;
	hrtimer_start(&timer, ms_to_ktime(SAMPLING_MS), HRTIMER_MODE_REL);

  return 0;

}

static void __exit my_exit(void)
{

  hrtimer_cancel(&timer); 


  misc_deregister(&nxp_simtemp);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Armando Correa");
MODULE_DESCRIPTION("A sample driver for registering a character device");