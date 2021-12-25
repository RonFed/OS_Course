#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define SUCCESS (0)
#define FAILURE (1)

#define True (1)
#define False (0)

volatile uint8_t g_server_is_running = False;

void sig_int_handler() {
    g_server_is_running = False;
}



int main(int argc, char const *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Number of arguments is wrong \n");
        return FAILURE;
    }

    uint16_t server_port = atoi(argv[1]);
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);

    signal(SIGINT, sig_int_handler);
    
    g_server_is_running = True;
    while (g_server_is_running == True) {

    }
    return 0;
}
