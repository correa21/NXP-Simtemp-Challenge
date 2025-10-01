#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include "nxp_simtemp.h"

#define DRIVER_NAME "nxp_simtemp"
#define DEVICE_NAME "simtemp"


static char text[64];
 
/* --- Temperature Simulation --- */

static s32 generate_temp(void)
{
	static s32 base_temp = 42000; /* 42.000 °C */
	s32 temp;
    
    temp = base_temp + (get_random_u32() % 200) - 100; /* +/- 0.1 °C jitter */

	return temp;
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

	if (sample.temp_mC >= 42000) {
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

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = my_open,
    .release = my_release,
    .read = my_read,
    .write = my_write
};

static struct miscdevice nxp_simtemp = {
    .name = DEVICE_NAME,
    .minor = MISC_DYNAMIC_MINOR,
    .fops = &fops
};

static int __init my_init(void)
{
  int status;
  printk("nxp_simtemp - Register misc device\n");
  status = misc_register(&nxp_simtemp);
  if (status) {
    pr_err("nxp_simtemp - Error during Register misc device\n");
    return -status;
  }
  return 0;

}

static void __exit my_exit(void)
{

  misc_deregister(&nxp_simtemp);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Armando Correa");
MODULE_DESCRIPTION("A sample driver for registering a character device");