#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>      /* for register_chrdev */
#include <linux/uaccess.h> /* for get_user and put_user */
#include <linux/string.h>  /* for memset*/

MODULE_LICENSE("GPL");

#include "message_slot.h"

// The message the device will give when asked
static char the_message[MAX_BUF_LEN];

/*======================== DEVICE FUNCTIONS =============================*/
static int device_open(struct inode *inode,
                       struct file *file)
{
    printk("Invoking device_open(%p)\n", file);
    return SUCCESS;
}

static int device_release(struct inode *inode,
                          struct file *file)
{
    printk("Invoking device_release(%p,%p)\n", inode, file);

    return SUCCESS;
}

static ssize_t device_read(struct file *file,
                           char __user *buffer,
                           size_t length,
                           loff_t *offset)
{
    printk("Invocing device_read(%p,%ld) - "
           "operation not supported yet\n"
           "(last written - %s)\n",
           file, length, the_message);

    copy_to_user(buffer, the_message, 3);

    return 3;
}

static ssize_t device_write(struct file *file,
                            const char __user *buffer,
                            size_t length,
                            loff_t *offset)
{
    int i;
    printk("Invoking device_write(%p,%ld)\n", file, length);
    for (i = 0; i < length && i < MAX_BUF_LEN; ++i)
    {
        get_user(the_message[i], &buffer[i]);
    }

    // return the number of input characters used
    return i;
}

//----------------------------------------------------------------
static long device_ioctl(struct file *file,
                         unsigned int ioctl_command_id,
                         unsigned long ioctl_param)
{
    return SUCCESS;
}

/*==================== DEVICE SETUP =============================*/

struct file_operations fops =
    {
        .owner = THIS_MODULE,
        .read = device_read,
        .write = device_write,
        .open = device_open,
        .unlocked_ioctl = device_ioctl,
        .release = device_release,
};

//---------------------------------------------------------------
// Initialize the module - Register the character device
static int __init simple_init(void)
{
    int rc = -1;
    // init dev struct
    //   memset( &device_info, 0, sizeof(struct chardev_info) );

    // Register driver capabilities.
    rc = register_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME, &fops);

    // Negative values signify an error
    if (rc < 0)
    {
        printk(KERN_ALERT "%s registraion failed for  %d\n",
               DEVICE_FILE_NAME, MAJOR_NUM);
        return rc;
    }

    printk("Registeration is successful. ");
    printk("If you want to talk to the device driver,\n");
    printk("you have to create a device file:\n");
    printk("mknod /dev/%s c %d 0\n", DEVICE_FILE_NAME, MAJOR_NUM);
    printk("You can echo/cat to/from the device file.\n");
    printk("Dont forget to rm the device file and "
           "rmmod when you're done\n");

    return 0;
}

//---------------------------------------------------------------
static void __exit simple_cleanup(void)
{
    // Unregister the device
    // Should always succeed
    unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);
}

module_init(simple_init);
module_exit(simple_cleanup);
