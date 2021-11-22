#include <linux/ioctl.h>

#define MAJOR_NUM           240

#define DEVICE_RANGE_NAME   "message_slot"
#define DEVICE_FILE_NAME    "message_slot"

#define SUCCESS             0

// Set the message of the device driver
#define MSG_SLOT_CHANNEL    _IOW(MAJOR_NUM, 0, unsigned long)

#define MAX_BUF_LEN         128
