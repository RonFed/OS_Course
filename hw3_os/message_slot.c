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
#include <linux/delay.h>


MODULE_LICENSE("GPL");

#include "message_slot.h"

/* Global array for message slots instances 
At most 256 since using register_chrdev() */
static msg_slot_t* g_msg_slots[MAX_MINORS_AMOUNT];

/*=======================================================================
========== CHANNELS DATA STRUCTURE FUNCTIONS - LINKED LIST===============
=======================================================================*/

static int add_channel_to_msg_slot(unsigned int slot_minor, msg_slot_channel_t** head, unsigned long channel_id) {
    msg_slot_channel_t* new_channel = (msg_slot_channel_t*)kmalloc(sizeof(msg_slot_channel_t), GFP_KERNEL);
    if (!new_channel) {
        printk(KERN_ERR "kmalloc failed in init_msg_slot");
        return -ENOMEM;
    }
    /* Add the new channel to the head of linked list of channels */
    new_channel->msg_buffer = NULL;
    new_channel->id = channel_id;
    new_channel->active = False;
    new_channel->next = (*head);

    (*head) = new_channel;
    // printk("Adding new channel_id (%ld)\n", channel_id);
    return SUCCESS;
}

static msg_slot_channel_t* find_channel(msg_slot_channel_t* head, unsigned long channel_id) {
    msg_slot_channel_t* tmp = head;
    while (tmp != NULL) {
        if (tmp->id == channel_id) {
            // printk("Found channel_id (%ld)\n", channel_id);
            return tmp;
        }
        tmp = tmp->next;
    }
    return NULL;
}

static void free_channel_lst(msg_slot_channel_t* head) {
    msg_slot_channel_t* tmp;
    while (head != NULL) {
        tmp = head;
        head = head->next;
        // printk("freeing channel (%ld)\n", tmp->id);
        kfree(tmp);
    }
}

/*=======================================================================
======================== DEVICE FUNCTIONS ===============================
=======================================================================*/

static int init_msg_slot(unsigned int minor) {
    printk("Init msg_slot minor is %d \n", minor);
    g_msg_slots[minor] = (msg_slot_t*)kmalloc(sizeof(msg_slot_t), GFP_KERNEL);
    if (!g_msg_slots[minor]) {
        printk(KERN_ERR "kmalloc failed in init_msg_slot");
        return -ENOMEM;
    }
    g_msg_slots[minor]->head = NULL;
    return SUCCESS;
}

static int device_open(struct inode *inode,
                       struct file *file) {
    unsigned int minor;
    // printk("Invoking device_open(%p)\n", file);
    minor = iminor(inode);
    
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
    // printk("Invoking device_release(%p,%p)\n", inode, file);

    return SUCCESS;
}

static ssize_t device_read(struct file *file, char __user *u_buffer, size_t length, loff_t *offset){
    unsigned long channel_id;
    unsigned int minor;
    msg_slot_t* msg_slot;
    msg_slot_channel_t* curr_channel;
    
    /* If no channel was assoicated with the file descriptor exit with error */
    if (file->private_data == NULL) {
        return -EINVAL;
    }

    channel_id = (unsigned long) file->private_data;
    minor = iminor(file->f_inode);
    msg_slot = g_msg_slots[minor];
    curr_channel = find_channel(msg_slot->head, channel_id);

    /* Check if no one wrote to this channel */
    if (curr_channel == NULL || curr_channel->active == False) {
        return -EWOULDBLOCK;
    }

    /* Check if the provided user buffer is big enough */
    if (length < curr_channel->curr_msg_len) {
        return -ENOSPC;
    }
    // printk("Invocing device_read minor is (%d)\n", minor);

    if (copy_to_user(u_buffer, curr_channel->msg_buffer, curr_channel->curr_msg_len)) {
        return -EFAULT;
    }

    return curr_channel->curr_msg_len;
}

static ssize_t device_write(struct file *file, const char __user *u_buffer, size_t length, loff_t *offset) {
    unsigned long channel_id;
    unsigned int minor;
    msg_slot_t* msg_slot;
    msg_slot_channel_t* curr_channel;

    /* If no channel was assoicated with the file descriptor exit with error */
    if (file->private_data == NULL) {
        return -EINVAL;
    }
    
    /* Check for invalid length */
    if (length == 0 || length > MAX_BUF_LEN) {
        return -EMSGSIZE;
    }

    channel_id = (unsigned long) file->private_data;
    minor = iminor(file->f_inode);
    msg_slot = g_msg_slots[minor];
    curr_channel = find_channel(msg_slot->head, channel_id);

    if (curr_channel == NULL) {
        /* Init channel if it hasn't been used and add it to the head of 
        channels linked list */
        if(add_channel_to_msg_slot(minor, &msg_slot->head, channel_id) == FAILURE) {
            /* Couldn't create new channel */
            return FAILURE;
        }
        /* In case of successfull add the new head will be the desired channel */
        curr_channel = msg_slot->head;
    }

    if (curr_channel->active == True) {
        /* Channel is active : i.e it's buffer was allocated in the past
        so we just realloc */
        curr_channel->msg_buffer = krealloc((void*)curr_channel->msg_buffer, length, GFP_KERNEL);
        if (!curr_channel->msg_buffer) {
            printk(KERN_ERR "kralloc failed in device_write");
            return -ENOMEM;
        }   
    } else {
        /* Channel not active, allocate memory and set to active*/
        curr_channel->msg_buffer = kmalloc(length, GFP_KERNEL);
        if (!curr_channel->msg_buffer) {
            printk(KERN_ERR "kralloc failed in device_write");
            return -ENOMEM;
        }   
        curr_channel->active = True;
    }

    if (copy_from_user((void*) curr_channel->msg_buffer, u_buffer, length)) {
        return -EFAULT; 
    }
    // printk("Wrote to channel (%ld) msg len is (%ld)\n", channel_id, length);
    curr_channel->curr_msg_len = (unsigned int) length;
    
    // return the number of input characters used
    return length;
}

static long device_ioctl(struct file *file,
                         unsigned int ioctl_command_id,
                         unsigned long ioctl_param)
{
    /* Validate command type */
    if (ioctl_command_id != MSG_SLOT_CHANNEL) {
        return -EINVAL;
    }
    /* Validate channel id */
    if (ioctl_param == 0) {
        return -EINVAL;
    }
    /* associate the passed channel id with the file descriptor */
    file->private_data = (void*) ioctl_param;

    return SUCCESS;
}

/*==================== DEVICE SETUP =============================*/

struct file_operations fops =
    {
        .owner          = THIS_MODULE,
        .read           = device_read,
        .write          = device_write,
        .open           = device_open,
        .unlocked_ioctl = device_ioctl,
        .release        = device_release,
};


/* Initialize the module - Register the character device */
static int __init msg_slot_init(void)
{
    int rc;

    /* Register driver */
    rc = register_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME, &fops);

    // Negative values signify an error
    if (rc < 0){
        printk(KERN_ERR "Failed to init module");
    }

    printk("Registeration is successful. \n");
    return 0;
}

static void __exit simple_cleanup(void) {
    int i;
    /* Free all allocated memory */
    for (i = 0; i < MAX_MINORS_AMOUNT; i++) {
        /* Find which message slots have been used */
        if (g_msg_slots[i] != NULL) {
            printk("Freeing msg_slot (%d)", i);
            free_channel_lst(g_msg_slots[i]->head);
            kfree(g_msg_slots[i]);
        }
    }
    
    // Unregister the device
    // Should always succeed
    unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);
}

module_init(msg_slot_init);
module_exit(simple_cleanup);
