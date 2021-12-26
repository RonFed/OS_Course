#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdint.h>
#include <unistd.h>
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

#define BUFFER_SZIE             (1024)

uint32_t pcc_total[PRINABLE_CHARS_AMOUNT];
uint32_t pcc_client[PRINABLE_CHARS_AMOUNT];
uint8_t recieve_buff[BUFFER_SZIE];

volatile uint8_t g_server_is_running = False;
volatile uint8_t g_server_is_processing = False;

void print_pcc_total() {
    for (int i = 0; i < PRINABLE_CHARS_AMOUNT; i++)
    {
       printf("char '%c' : %u times\n", i + MIN_PRINTABLE_CHAR, pcc_total[i]);
    }
}

void sig_int_handler() {
    if (g_server_is_processing) {
        g_server_is_running = False;
    } else {
        print_pcc_total();
        exit(EXIT_SUCCESS);
    }
}

int is_printable(uint8_t c) {
    return (c >= MIN_PRINTABLE_CHAR) && (c <= MAX_PRINTABLE_CHAR);
}

uint32_t update_pcc_client(int bytes_num) {
    uint32_t printable_counter = 0;
    for (int i = 0; i < bytes_num; i++)
    {
        uint8_t curr_char = recieve_buff[i];
        if (is_printable(curr_char)) {
            pcc_client[curr_char - MIN_PRINTABLE_CHAR]++;
            printable_counter++;
        }
    }
    return printable_counter;
    
}

void print_client_pcc() {
    for (int i = 0; i < PRINABLE_CHARS_AMOUNT; i++)
    {
       printf("char '%c' : %u times\n", i + MIN_PRINTABLE_CHAR, pcc_client[i]);
    }
}

void update_pcc_total() {
    for (int i = 0; i < PRINABLE_CHARS_AMOUNT; i++)
    {
        pcc_total[i] += pcc_client[i];
    }
}

void reset_pcc_client() {
    for (int i = 0; i < PRINABLE_CHARS_AMOUNT; i++)
    {
        pcc_client[i] = 0;
    }
}

int main(int argc, char const *argv[])
{
    uint16_t server_port;
    int socket_fd;
    int connection_fd;
    struct sockaddr_in serv_addr;
    uint32_t stream_byte_size = 0;
    char* stream_size_ptr;

    if (argc != 2) {
        fprintf(stderr, "Number of arguments is wrong \n");
        return FAILURE;
    }

    server_port = atoi(argv[1]);

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        fprintf(stderr, "%s \n", strerror(errno));
        return FAILURE;
    }

    memset(&serv_addr, 0, sizeof(struct sockaddr_in));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(server_port);

    if (bind(socket_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "%s \n", strerror(errno));
        return FAILURE;
    }

     if (listen(socket_fd, LISTEN_QUEUE_SIZE) < 0) {
        fprintf(stderr, "%s \n", strerror(errno));
        return FAILURE;
     }

    int on = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        fprintf(stderr, "%s \n", strerror(errno));
        return FAILURE;
    }

    signal(SIGINT, sig_int_handler);
    
    g_server_is_running = True;

    while (g_server_is_running) {
        printf("server available\n");
        g_server_is_processing = False;
        connection_fd = accept(socket_fd, NULL, NULL);
        printf("server accepted a job\n");
        if (connection_fd == -1) {
            fprintf(stderr, "accept() failed : %s \n", strerror(errno));
            return FAILURE;
        } else {
            g_server_is_processing = True;
        }
        uint32_t printable_counter = 0;
        int total_bytes_written = 0;

        /* Read the stream size from client */
        stream_size_ptr = (char*) &stream_byte_size;
        uint32_t total_bytes_read = 0;
        do
        {
            int bytes_read = read(connection_fd, 
                                stream_size_ptr + total_bytes_read, 
                                sizeof(uint32_t) - total_bytes_read);
            
            if (bytes_read < 0) {
                // TODO
                printf("read() failed : %s\n", strerror(errno));
                return FAILURE;
            } else {
                printf("server read %d bytes\n", bytes_read);
                total_bytes_read+=bytes_read;
            }
        } while (total_bytes_read < sizeof(uint32_t));
        
        stream_byte_size = ntohl(stream_byte_size);
        printf("stream size is %u \n", stream_byte_size);

        /* Read the stream from client */
        total_bytes_read = 0;
        reset_pcc_client();
        while (total_bytes_read < stream_byte_size) {
            int bytes_read = read(connection_fd, recieve_buff, BUFFER_SZIE);
            // printf("read %d bytes \n", bytes_read);
            if (bytes_read  == 0 ) {
                break;
            }
            // printf("hi\n");
            if (bytes_read < 0) {
                // TODO
                printf("read() failed : %s\n", strerror(errno));
                return FAILURE;
            } else {
                total_bytes_read+=bytes_read;
                printable_counter += update_pcc_client(bytes_read);
            }
        }
       
        printf("finished stream read\n");
        /* Send to client number of printable charcters */
        printable_counter = htonl(printable_counter);
        uint8_t* write_data = (uint8_t*)&printable_counter;
        do
        {
            int bytes_written = write(connection_fd,
                                    write_data + total_bytes_written,
                                    sizeof(uint32_t) - total_bytes_written);
            printf("Server sends result %d bytes\n", bytes_written);
            if (bytes_written <= 0) {
                // TODO
                printf("write() failed : %s\n", strerror(errno));
                return FAILURE;
            } else {
                total_bytes_written += bytes_written;
            }
        } while (total_bytes_written < sizeof(uint32_t));
        
        printf("updating pcc_total\n");
        update_pcc_total();
        printf("finished updating\n");
        // print_client_pcc();
    }

    print_pcc_total();
    return SUCCESS;
}
