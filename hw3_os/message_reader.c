#include <fcntl.h>     /* open */
#include <unistd.h>    /* exit */
#include <sys/ioctl.h> /* ioctl */
#include <unistd.h>   /* write() */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "message_slot.h"

int main(int argc, char const *argv[])
{
    if (argc != 3)
    {
        errno = E2BIG;
        printf("%s \n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    char const *msg_slot_path = argv[1];
    unsigned int channel_id = atoi(argv[2]);

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

    char* buffer = (char*) malloc(MAX_BUF_LEN);
    /* Read the last message in the channel */
    ret_val = read(file_desc, buffer, MAX_BUF_LEN);
    if (ret_val < 0) {
        printf("%s \n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Close the device */
    close(file_desc);

    /* Print the message to stdout (ret val is the length of
    the message in bytes which was returned from read() ) */
    ret_val = write(STDOUT_FILENO, buffer, ret_val);
    if (ret_val < 0) {
        printf("%s \n", strerror(errno));
        exit(EXIT_FAILURE);
    }


    return 0;
}