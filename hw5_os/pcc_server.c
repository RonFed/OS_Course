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

#define LISTEN_QUEUE_SIZE (10)

#define MIN_PRINTABLE_CHAR (32)
#define MAX_PRINTABLE_CHAR (126)
#define PRINABLE_CHARS_AMOUNT (MAX_PRINTABLE_CHAR) - (MIN_PRINTABLE_CHAR) + 1

#define BUFFER_SZIE (1024)

uint32_t pcc_total[PRINABLE_CHARS_AMOUNT];
uint32_t pcc_client[PRINABLE_CHARS_AMOUNT];
uint8_t recieve_buff[BUFFER_SZIE];

volatile uint8_t g_server_is_running = False;
volatile uint8_t g_server_is_processing = False;

void print_pcc_total()
{
    for (int i = 0; i < PRINABLE_CHARS_AMOUNT; i++)
    {
        printf("char '%c' : %u times\n", i + MIN_PRINTABLE_CHAR, pcc_total[i]);
    }
}

/* SIGINT handler */
void sig_int_handler()
{
    if (g_server_is_processing)
    {
        /* The server is in processing so mark the flag as False,
        when it'll finish processing the main server loop will exit and print total results */
        g_server_is_running = False;
    }
    else
    {
        /* Server is idle and requested to exit, print total results and exit */
        print_pcc_total();
        exit(EXIT_SUCCESS);
    }
}

int is_printable(uint8_t c)
{
    return (c >= MIN_PRINTABLE_CHAR) && (c <= MAX_PRINTABLE_CHAR);
}

uint32_t update_pcc_client(int bytes_num)
{
    uint32_t printable_counter = 0;
    for (int i = 0; i < bytes_num; i++)
    {
        uint8_t curr_char = recieve_buff[i];
        if (is_printable(curr_char))
        {
            pcc_client[curr_char - MIN_PRINTABLE_CHAR]++;
            printable_counter++;
        }
    }
    return printable_counter;
}

void update_pcc_total()
{
    for (int i = 0; i < PRINABLE_CHARS_AMOUNT; i++)
    {
        pcc_total[i] += pcc_client[i];
    }
}

void reset_pcc_client()
{
    for (int i = 0; i < PRINABLE_CHARS_AMOUNT; i++)
    {
        pcc_client[i] = 0;
    }
}

uint32_t read_stream_size_from_client(int connection_fd, int* error_status)
{
    char *stream_size_ptr;
    uint32_t stream_byte_size = 0;
    stream_size_ptr = (char *)&stream_byte_size;
    uint32_t total_bytes_read = 0;
    do
    {
        int bytes_read = read(connection_fd,
                              stream_size_ptr + total_bytes_read,
                              sizeof(uint32_t) - total_bytes_read);

        if (bytes_read <= 0)
        {
            printf("read() failed : %s\n", strerror(errno));
            *error_status = True;
            return 0;
        }
        else
        {
            total_bytes_read += bytes_read;
        }
    } while (total_bytes_read < sizeof(uint32_t));

    stream_byte_size = ntohl(stream_byte_size);
    return stream_byte_size;
}

uint32_t read_stream_from_client(uint32_t stream_byte_size, int connection_fd, int* error_status)
{
    uint32_t total_bytes_read = 0;
    uint32_t printable_counter = 0;

    while (total_bytes_read < stream_byte_size)
    {
        int bytes_read = read(connection_fd, recieve_buff, BUFFER_SZIE);
        if (bytes_read == 0)
        {
            fprintf(stderr, "Client connection ended while sending stream\n");
            *error_status = True;
            return 0;
        }
        if (bytes_read < 0)
        {
            fprintf(stderr,"read() failed : %s\n", strerror(errno));
            *error_status = True;
            return 0;
        }
        else
        {
            total_bytes_read += bytes_read;
            printable_counter += update_pcc_client(bytes_read);
        }
    }

    /* This function returns the printable characters counted from the strem in 
    host format */
    return printable_counter;
}

int send_printable_counter_to_client(int connection_fd, uint8_t* write_data, int* error_status)
{
    int total_bytes_written = 0;
    do
    {
        int bytes_written = write(connection_fd,
                                  write_data + total_bytes_written,
                                  sizeof(uint32_t) - total_bytes_written);
        if (bytes_written <= 0)
        {
            fprintf(stderr, "write() failed : %s\n", strerror(errno));
            *error_status = True;
            return FAILURE;
        }
        else
        {
            total_bytes_written += bytes_written;
        }
    } while (total_bytes_written < sizeof(uint32_t));
    return SUCCESS;
}

int main(int argc, char const *argv[])
{
    uint16_t server_port;
    int socket_fd;
    int connection_fd;
    struct sockaddr_in serv_addr;
    uint32_t stream_byte_size = 0;

    if (argc != 2)
    {
        fprintf(stderr, "Number of arguments is wrong \n");
        return FAILURE;
    }

    server_port = atoi(argv[1]);

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        fprintf(stderr, "%s \n", strerror(errno));
        return FAILURE;
    }

    memset(&serv_addr, 0, sizeof(struct sockaddr_in));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(server_port);

    if (bind(socket_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        fprintf(stderr, "%s \n", strerror(errno));
        return FAILURE;
    }

    if (listen(socket_fd, LISTEN_QUEUE_SIZE) < 0)
    {
        fprintf(stderr, "%s \n", strerror(errno));
        return FAILURE;
    }

    int on = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
    {
        fprintf(stderr, "%s \n", strerror(errno));
        return FAILURE;
    }

    signal(SIGINT, sig_int_handler);

    g_server_is_running = True;

    while (g_server_is_running)
    {
        g_server_is_processing = False;
        connection_fd = accept(socket_fd, NULL, NULL);
        if (connection_fd == -1)
        {
            fprintf(stderr, "accept() failed : %s \n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        else
        {
            g_server_is_processing = True;
        }
        uint32_t printable_counter = 0;
        /* this variable is used to detect errors regarding the connection
        with the client, in case one of the following steps results in such error
        we will continue to next client and the total pcc won't be updated*/
        int error_status = False;

        /* Read the stream size from client */
        stream_byte_size = read_stream_size_from_client(connection_fd, &error_status);
        if (error_status == True) {
            continue;
        }
        /* reset the pcc count for current client */
        reset_pcc_client();
        
        /* Read the stream from client 
        this operation will update the current pcc_client struct and will return the 
        printable chars count for the current client 
        Next, convert the result to network format*/
        printable_counter = read_stream_from_client(stream_byte_size, connection_fd, &error_status);
        if (error_status == True) {
            continue;
        } else {
            printable_counter = htonl(printable_counter);
        }

        uint8_t *printable_counter_ptr = (uint8_t *)&printable_counter;
        /* Send to client number of printable charcters (in network format) */
        send_printable_counter_to_client(connection_fd, printable_counter_ptr, &error_status);
        if (error_status == True) {
            continue;
        } else {
            /* Use the pcc for client to update the total pcc struct*/
            update_pcc_total();
        }
    }

    print_pcc_total();
    return SUCCESS;
}
