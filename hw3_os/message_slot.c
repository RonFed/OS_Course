#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>      /* for register_chrdev */
#include <linux/uaccess.h> /* for get_user and put_user */
#include <linux/string.h>  /* for memset*/
#include <linux/slab.h>

MODULE_LICENSE("GPL");

#include "message_slot.h"

/* Global array for message slots instances 
At most 256 since using register_chrdev() */
static msg_slot_t* g_msg_slots[MAX_MINORS_AMOUNT];

/*=======================================================================
============== CHANNELS DATA STRUCTURE FUNCTIONS ========================
=======================================================================*/

static int add_channel_to_msg_slot(unsigned int slot_minor, msg_slot_channel_t** head, unsigned long channel_id) {
    msg_slot_channel_t* new_channel = (msg_slot_channel_t*)kmalloc(sizeof(msg_slot_channel_t), GFP_KERNEL);
    if (!new_channel) {
        printk(KERN_ERR "kmalloc failed in init_msg_slot");
        errno = ENOMEM;
        return FAILURE;
    }
    /* Add the new channel to the head of linked list of channels */
    new_channel->msg_buffer = NULL;
    new_channel->id = channel_id;
    new_channel->active = False;
    new_channel->next = (*head);

    (*head) = new_channel;
    return SUCCESS;
}

static msg_slot_channel_t* find_channel(msg_slot_channel_t* head, unsigned long channel_id) {
    msg_slot_channel_t* tmp = head;
    while (tmp != NULL) {
        if (tmp->id == channel_id) {
            return tmp;
        }
        tmp = tmp->next;
    }
    return NULL;
}

static int init_msg_slot(unsigned int minor) {
    g_msg_slots[minor] = (msg_slot_t*)kmalloc(sizeof(msg_slot_t), GFP_KERNEL);
    if (!g_msg_slots[minor]) {
        printk(KERN_ERR "kmalloc failed in init_msg_slot");
        errno = ENOMEM;
        return FAILURE;
    }
    g_msg_slots[minor]->head = NULL;
    return SUCCESS;
}

// The message the device will give when asked
static char the_message[MAX_BUF_LEN];

/*=======================================================================
======================== DEVICE FUNCTIONS ===============================
=======================================================================*/

static int device_open(struct inode *inode,
                       struct file *file) {
    printk("Invoking device_open(%p)\n", file);
    unsigned int minor = iminor(inode);
    
    /* if the minor hasn't been used before, allocate memory for it */
    if (g_msg_slots[minor] == NULL) {
        return init_msg_slot(minor);
    }
    
    /* We get here if the minor has been created in the past */
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

static ssize_t device_write(struct file *file, const char __user *buffer, size_t length, loff_t *offset) {
    /* If no channel was assoicated with the file descriptor exit with error */
    if (file->private_data == NULL) {
        errno = EINVAL;
        return FAILURE;
    }
    
    /* Check for invalid length */
    if (length == 0 | length > MAX_BUF_LEN) {
        errno = EMSGSIZE;
        return FAILURE;
    }

    unsigned long channel = (unsigned long) file->private_data;
    unsigned int minor = minor(file->f_inode);
    msg_slot_t* msg_slot = g_msg_slots[minor];
    msg_slot_channel_t* curr_channel = find_channel(msg_slot->head, channel);

    if (curr_channel == NULL) {
        if(add_channel_to_msg_slot(minor, &msg_slot->head, channel) == FAILURE) {
            /* Couldn't create new channel */
            return FAILURE;
        }
        /* In case of successfull add the new head will be the desired channel */
        curr_channel = msg_slot->head;
    }

    if (curr_channel->active == True) {
        
    }
    int i;
    printk("Invoking device_write(%p,%ld)\n", file, length);
    for (i = 0; i < length && i < MAX_BUF_LEN; ++i)
    {
        get_user(the_message[i], &buffer[i]);
    }

    // return the number of input characters used
    return i;
}

static long device_ioctl(struct file *file,
                         unsigned int ioctl_command_id,
                         unsigned long ioctl_param)
{
    /* Validate command type */
    if (ioctl_command_id != MSG_SLOT_CHANNEL) {
        errno = EINVAL;
        return FAILURE;
    }
    /* Validate channel id */
    if (ioctl_param == 0) {
        errno = EINVAL;
        return FAILURE;
    }
    /* associate the passed channel id with the file descriptor */
    file->private_data = (void*) ioctl_param;

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


/* Initialize the module - Register the character device 
and init global memory for driver use */
static int __init msg_slot_init(void)
{
    /* Init the global array for diffrent instances of the module */
    for (int i = 0; i < MAX_MINORS_AMOUNT; i++) {
        memset(g_msg_slots[i], 0, sizeof(msg_slot_t));
    }

    /* Register driver */
    int rc = register_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME, &fops);

    // Negative values signify an error
    if (rc < 0){
        printk(KERN_ERR "Failed to init module");
    }

    printk("Registeration is successful. ");
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
