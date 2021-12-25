#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>

#define SUCCESS (0)
#define FAILURE (1)

#define True (1)
#define False (0)

#define LISTEN_QUEUE_SIZE       (10)

#define MIN_PRINTABLE_CHAR      (32)
#define MAX_PRINTABLE_CHAR      (126)
#define PRINABLE_CHARS_AMOUNT   (MAX_PRINTABLE_CHAR)-(MIN_PRINTABLE_CHAR)+1

uint8_t pcc_total[PRINABLE_CHARS_AMOUNT];

volatile uint8_t g_server_is_running = False;
volatile uint8_t g_server_is_processing = False;

void sig_int_handler() {
    if (g_server_is_processing) {
        g_server_is_running = False;
    } else {
        exit(EXIT_SUCCESS);
    }
}


int main(int argc, char const *argv[])
{
    uint16_t server_port;
    int socket_fd;
    int connection_fd;
    struct sockaddr_in serv_addr;
    uint32_t stream_byte_size = 0;
    uint8_t* stream_size_ptr;

    if (argc != 2) {
        fprintf(stderr, "Number of arguments is wrong \n");
        return FAILURE;
    }

    server_port = atoi(argv[1]);

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        fprintf(stderr, "%s \n", strerror(errno));
        return FAILURE;
    }

    memset(&serv_addr, 0, sizeof(struct sockaddr_in));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(server_port);

    if (bind(socket_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) == -1) {
        fprintf(stderr, "%s \n", strerror(errno));
        return FAILURE;
    }

     if (listen(socket_fd, LISTEN_QUEUE_SIZE) == -1) {
        fprintf(stderr, "%s \n", strerror(errno));
        return FAILURE;
     }

    signal(SIGINT, sig_int_handler);
    
    g_server_is_running = True;

    while (g_server_is_running) {
        g_server_is_processing = False;
        connection_fd = accept(socket_fd, NULL, NULL);
        if (connection_fd == -1) {
            fprintf(stderr, "%s \n", strerror(errno));
            return FAILURE;
        } else {
            g_server_is_processing = True;
        }

        stream_size_ptr = (uint8_t*) &stream_byte_size;
        uint8_t total_bytes_read = 0;
        do
        {
            int bytes_read = read(socket_fd, stream_size_ptr, sizeof(uint32_t) - total_bytes_read);
            if (bytes_read <= 0) {
                // TODO
            } else {
                total_bytes_read+=bytes_read;
            }
        } while (total_bytes_read < sizeof(uint32_t));
        
        stream_byte_size = ntohl(stream_byte_size);
        printf("stram size is %d \n", stream_byte_size);

        total_bytes_read = 0;
        do
        {
            // int bytes_read = read(socket_fd, stream_size_ptr, stream_byte_size - total_bytes_read);
            if (bytes_read <= 0) {
                // TODO
            } else {
                total_bytes_read+=bytes_read;
            }
        } while (total_bytes_read < stream_byte_size);


    }
    return 0;
}
