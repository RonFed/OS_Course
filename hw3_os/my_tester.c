#include <fcntl.h>     /* open */
#include <unistd.h>    /* exit */
#include <sys/ioctl.h> /* ioctl */
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "message_slot.h"

#define SENDER_PATH "./sender.o"
#define READER_PATH "./reader.o"
#define SLOT0_PATH "/dev/slot0"
#define SLOT1_PATH "/dev/slot1"
#define CHUNNELS_MAX (1024 * 1024)

void sigchld_handler(int signum, siginfo_t * info, void * unused) {
    int status, pid;
    while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED)) > 0) {
        if (pid < 0) {
            printf("this is bad");
        }
    };

    return;
}

int main(int argc, char const *argv[])
{
    int channel, pid;
    char channel_text[20];
    char msg[20];

    struct sigaction sigchld_action;
    memset(&sigchld_action, 0, sizeof(sigchld_action));

    sigchld_action.sa_sigaction = sigchld_handler;
    sigchld_action.sa_flags = SA_RESTART | SA_SIGINFO;
    
    sigaction(SIGCHLD, &sigchld_action, NULL);

    // char *reader_exec_args[] = {READER_PATH, NULL, NULL, NULL };
    char *sender_exec_args[] = {SENDER_PATH, NULL, NULL, NULL, NULL};

    // reader_exec_args[1] = SLOT0_PATH;
    sender_exec_args[1] = SLOT0_PATH;

    printf("Started sending messages \n");
    for (channel = 1; channel < CHUNNELS_MAX; channel++)
    {
        sprintf(channel_text, "%d", channel);
        // printf("%s \n", channel_text);
        sprintf(msg, "hi %d", channel);
        // printf("%s \n", msg);
        sender_exec_args[2] = channel_text;
        sender_exec_args[3] = msg;
        pid = fork();
        if (pid == -1)
        {
            printf("on channel %d ", channel);
            printf("%s \n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (pid == 0)
        {
            if (execvp(SENDER_PATH, sender_exec_args) == -1)
            {
                perror("Failed executing aux");
                exit(EXIT_FAILURE);
            }
        }
    }
    printf("Finished sending messages to %d channels \n", channel);

    return 0;
}
