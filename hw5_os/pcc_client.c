#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>

#define SUCCESS (0)
#define FAILURE (1)

#define True (1)
#define False (0)

#define BUFFER_SZIE (1024)

uint32_t input_file_length(const char *pathname)
{
    struct stat file_stat;
    if (stat(pathname, &file_stat) == -1)
    {
        fprintf(stderr, "%s \n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    return file_stat.st_size;
}

void write_file_to_socket(int socket_fd, int file_fd, int bytes_to_send)
{
    uint8_t buffer[BUFFER_SZIE];
    int total_bytes_read = 0;
    int current_bytes_read = 0;
    while (total_bytes_read < bytes_to_send)
    {
        /* *Fill the buffer from the file */
        current_bytes_read = read(file_fd, buffer, BUFFER_SZIE);
        if (current_bytes_read == 0)
        {
            break;
        }
        if (current_bytes_read < 0)
        {
            fprintf(stderr, "%s \n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        total_bytes_read += current_bytes_read;
        uint8_t *buffer_ptr = buffer;
        int bytes_left_to_send = current_bytes_read;
        /* Send the buffer content to socket */
        while (bytes_left_to_send > 0)
        {
            int bytes_written = write(socket_fd, buffer_ptr, bytes_left_to_send);
            if (bytes_written < 0)
            {
                fprintf(stderr, "%s \n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            bytes_left_to_send -= bytes_written;
            buffer_ptr += bytes_written;
        }
    }
}

void read_result_from_server(int socket_fd, uint8_t *data_ptr)
{
    uint8_t total_bytes_read = 0;
    do
    {
        int bytes_read = read(socket_fd,
                              data_ptr + total_bytes_read,
                              sizeof(uint32_t) - total_bytes_read);
        if (bytes_read < 0)
        {
            fprintf(stderr, "%s \n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        else
        {
            total_bytes_read += bytes_read;
        }
    } while (total_bytes_read < sizeof(uint32_t));
}

void write_stream_size_to_server(int socket_fd, uint8_t* stream_size_ptr) {
    uint8_t total_bytes_written = 0;
    do
    {
        int bytes_written = write(socket_fd,
                                  stream_size_ptr + total_bytes_written,
                                  sizeof(uint32_t) - total_bytes_written);
        if (bytes_written < 0)
        {
            /* On any error condition, print an error message to stderr containing the errno string (i.e., with
                perror() or strerror()) and exit with exit code 1.*/
            fprintf(stderr, "%s \n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        else
        {
            total_bytes_written += bytes_written;
        }
    } while (total_bytes_written < sizeof(uint32_t));
}

int main(int argc, char const *argv[])
{
    /*  argv[1]: server’s IP address (assume a valid IP address).
        argv[2]: server’s port (assume a 16-bit unsigned integer).
        argv[3]: path of the file to send. */

    struct sockaddr_in server_addr;
    int socket_fd = -1;
    int input_file_fd = -1;

    if (argc != 4)
    {
        fprintf(stderr, "Number of arguments is wrong \n");
        exit(EXIT_FAILURE);
    }

    input_file_fd = open(argv[3], O_RDONLY);
    if (input_file_fd < 0)
    {
        fprintf(stderr, "%s \n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    uint32_t file_length = input_file_length(argv[3]);
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        fprintf(stderr, "%s \n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    uint16_t server_port = (uint16_t)atoi(argv[2]);
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    if (inet_pton(AF_INET, argv[1], &(server_addr.sin_addr)) == 0) {
        fprintf(stderr, "%s \n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "%s \n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Wrie the stream size to server 
    convert the file lenth to network format */
    uint32_t file_length_net = htonl(file_length);
    uint8_t *stream_size_ptr = (uint8_t *)&file_length_net;

    write_stream_size_to_server(socket_fd, stream_size_ptr);

    write_file_to_socket(socket_fd, input_file_fd, file_length);

    uint32_t pcc_from_server = 0;
    read_result_from_server(socket_fd, (uint8_t*)&pcc_from_server);
    /* Convert the result from server to hos format */
    pcc_from_server = ntohl(pcc_from_server);

    printf("# of printable characters: %u\n", pcc_from_server);
    return 0;
}
