#include <linux/ioctl.h>

#define MAJOR_NUM 240

// Set the message of the device driver
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUM, 0, unsigned long)

#define MAX_BUF_LEN 128
