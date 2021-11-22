#include <fcntl.h>     /* open */
#include <unistd.h>    /* exit */
#include <sys/ioctl.h> /* ioctl */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "message_slot.h"

int main(int argc, char const *argv[])
{
    if (argc != 4)
    {
        errno = E2BIG;
        printf("%s \n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    char const *msg_slot_path = argv[1];
    unsigned int channel_id = atoi(argv[2]);
    char const *msg_to_pass = argv[3];

    int file_desc;
    int ret_val;

    file_desc = open(msg_slot_path, O_RDWR);

    if (file_desc < 0)
    {
        printf("%s \n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    ret_val = ioctl(file_desc, MSG_SLOT_CHANNEL, channel_id);
    if (ret_val < 0) {
        printf("%s \n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    ret_val = write(file_desc, msg_to_pass, strlen(msg_to_pass) - 1);
    if (ret_val < 0) {
        printf("%s \n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    close(file_desc);
    return 0;
}
