#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>

static int major;

static struct file_operations fops ={};

static int __init my_init(void);
static void __exit my_exit(void);

static int __init my_init(void)
{
  major = register_chrdev(0, "hello_cdev", &fops);//use 0 for allocating any free major device number
  if (major < 0) {
    pr_err("hello_cdev - Error registering chrdev\n");
    return major;
  }
  pr_info("hello_cdev - Major Device Number: %d\n", major);
  return 0;

}

static void __exit my_exit(void)
{

  unregister_chrdev(major, "hello_cdev");

}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Armando Correa");
MODULE_DESCRIPTION("A sample driver for registering a character device");