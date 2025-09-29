#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include "nxp_simtemp.h"

static int __init my_init(void);
static void __exit my_exit(void);

static struct file_operations fops = {
    .owner = THIS_MODULE,
};

static struct miscdevice nxp_simtemp = {
    .name = "simtemp",
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