#include <linux/ioctl.h>

#define MAJOR_NUM           240

#define DEVICE_RANGE_NAME   "message_slot"
#define DEVICE_FILE_NAME    "message_slot"

#define SUCCESS             0
#define FAILURE             -1

#define True                1
#define False               0

// Set the message of the device driver
#define MSG_SLOT_CHANNEL    _IOW(MAJOR_NUM, 0, unsigned long)

#define MAX_BUF_LEN         128
#define MAX_MINORS_AMOUNT   256

typedef struct msg_slot_channel_t {
    void* msg_buffer;
    unsigned int curr_msg_len;
    unsigned long id;
    unsigned int active;
    struct msg_slot_channel_t* next;
} msg_slot_channel_t;

typedef struct {
    msg_slot_channel_t* head;
} msg_slot_t;