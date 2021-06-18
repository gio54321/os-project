#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "file_storage_api.h"
#include "protocol.h"

// global socket fd
int socket_fd = -1;

static int receive_files_from_server(const char* dirname)
{
    for (;;) {
        // receive the response
        struct packet response;
        clear_packet(&response);
        int receive_res = receive_packet(socket_fd, &response);
        if (receive_res <= 0) {
            errno = EIO;
            return -1;
        }
        // if the response is COMP then the operation terminated successfully
        if (response.op == COMP) {
            return 0;
        }

        // if the response is an error then print it to stderr
        if (response.op == ERROR) {
            print_error_code(response.err_code, "receive files form server");
            errno = EBADE;
            return -1;
        }
        if (response.op == FILE_P) {
            size_t dirname_len = strlen(dirname);
            char* abs_path = malloc((dirname_len + response.name_length + 2) * sizeof(char));
            strcpy(abs_path, dirname);
            abs_path[dirname_len] = '/';
            strcpy(abs_path + dirname_len + 1, response.filename);
            printf("absolute path: %s\n", abs_path);
            if (response.data_size > 0) {
                FILE* fd;
                fd = fopen(abs_path, "w+");
                if (fd == NULL) {
                    return -1;
                }

                fwrite(response.data, sizeof(char), response.data_size, fd);
                fclose(fd);
            }
        }
    }
}

int openConnection(const char* sockname, int msec, const struct timespec abstime)
{
    // TODO implement timeout logic
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, sockname, strlen(sockname) + 1);
    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        perror("socket");
        return -1;
    }
    int connect_res = connect(socket_fd, (struct sockaddr*)&sa, sizeof(struct sockaddr_un));
    if (connect_res == -1) {
        perror("connect");
        return -1;
    }
    return 0;
}

int closeConnection(const char* sockname)
{
    if (sockname == NULL) {
        errno = EINVAL;
        return -1;
    }
    int close_res = close(socket_fd);
    return close_res;
}

int openFile(const char* pathname, int flags)
{
    if (pathname == NULL) {
        errno = EINVAL;
        return -1;
    }
    // send the request to the server
    struct packet request;
    clear_packet(&request);
    request.op = OPEN_FILE;
    request.name_length = strlen(pathname);
    request.filename = malloc((strlen(pathname) + 1) * sizeof(char));
    if (request.filename == NULL) {
        errno = ENOMEM;
        return -1;
    }
    strcpy(request.filename, pathname);
    request.flags = flags;
    int send_res = send_packet(socket_fd, &request);
    if (send_res <= 0) {
        errno = EIO;
        return -1;
    }
    free(request.filename);

    // receive the response
    struct packet response;
    clear_packet(&response);
    int receive_res = receive_packet(socket_fd, &response);
    if (receive_res <= 0) {
        errno = EIO;
        return -1;
    }
    // if the response is COMP then the operation terminated successfully
    if (response.op == COMP) {
        return 0;
    }

    // if the response is an error then print it to stderr
    if (response.op == ERROR) {
        print_error_code(response.err_code, "openFile");
    }
    errno = EBADE;
    return -1;
}

int readFile(const char* pathname, void** buf, size_t* size)
{
    if (pathname == NULL) {
        errno = EINVAL;
        return -1;
    }
    // send the request to the server
    struct packet request;
    clear_packet(&request);
    request.op = READ_FILE;
    request.name_length = strlen(pathname);
    request.filename = malloc((strlen(pathname) + 1) * sizeof(char));
    if (request.filename == NULL) {
        errno = ENOMEM;
        return -1;
    }
    strcpy(request.filename, pathname);
    int send_res = send_packet(socket_fd, &request);
    if (send_res <= 0) {
        errno = EIO;
        return -1;
    }
    free(request.filename);

    // receive the response
    struct packet response;
    clear_packet(&response);
    int receive_res = receive_packet(socket_fd, &response);
    if (receive_res <= 0) {
        errno = EIO;
        return -1;
    }
    // if the response is COMP then the operation terminated successfully
    if (response.op == DATA) {
        // the buffer is already allocated on the head by the call to receive_packet
        *buf = response.data;
        *size = response.data_size;
        return 0;
    }

    // if the response is an error then print it to stderr
    if (response.op == ERROR) {
        print_error_code(response.err_code, "readFile");
    }
    errno = EBADE;
    return -1;
}

int readNFiles(int n, const char* dirname)
{
    if (dirname == NULL || n < -1) {
        errno = EINVAL;
        return -1;
    }
    // send the request to the server
    struct packet request;
    clear_packet(&request);
    request.op = READ_N_FILES;
    request.count = n;
    int send_res = send_packet(socket_fd, &request);
    if (send_res <= 0) {
        errno = EIO;
        return -1;
    }
    printf("RECEIVED\n");

    return receive_files_from_server(dirname);
}

int writeFile(const char* pathname, const char* dirname);
int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname);

int lockFile(const char* pathname);
int unlockFile(const char* pathname);

int closeFile(const char* pathname)
{
    if (pathname == NULL) {
        errno = EINVAL;
        return -1;
    }
    // send the request to the server
    struct packet request;
    clear_packet(&request);
    request.op = CLOSE_FILE;
    request.name_length = strlen(pathname);
    request.filename = malloc((strlen(pathname) + 1) * sizeof(char));
    if (request.filename == NULL) {
        errno = ENOMEM;
        return -1;
    }
    strcpy(request.filename, pathname);
    int send_res = send_packet(socket_fd, &request);
    if (send_res <= 0) {
        errno = EIO;
        return -1;
    }
    free(request.filename);

    // receive the response
    struct packet response;
    clear_packet(&response);
    int receive_res = receive_packet(socket_fd, &response);
    if (receive_res <= 0) {
        errno = EIO;
        return -1;
    }
    // if the response is COMP then the operation terminated successfully
    if (response.op == COMP) {
        return 0;
    }

    // if the response is an error then print it to stderr
    if (response.op == ERROR) {
        print_error_code(response.err_code, "closeFile");
    }
    errno = EBADE;
    return -1;
}

int removeFile(const char* pathname)
{
    if (pathname == NULL) {
        errno = EINVAL;
        return -1;
    }
    // send the request to the server
    struct packet request;
    clear_packet(&request);
    request.op = REMOVE_FILE;
    request.name_length = strlen(pathname);
    request.filename = malloc((strlen(pathname) + 1) * sizeof(char));
    if (request.filename == NULL) {
        errno = ENOMEM;
        return -1;
    }
    strcpy(request.filename, pathname);
    int send_res = send_packet(socket_fd, &request);
    if (send_res <= 0) {
        errno = EIO;
        return -1;
    }
    free(request.filename);

    // receive the response
    struct packet response;
    clear_packet(&response);
    int receive_res = receive_packet(socket_fd, &response);
    if (receive_res <= 0) {
        errno = EIO;
        return -1;
    }
    // if the response is COMP then the operation terminated successfully
    if (response.op == COMP) {
        return 0;
    }

    // if the response is an error then print it to stderr
    if (response.op == ERROR) {
        print_error_code(response.err_code, "removeFile");
    }
    errno = EBADE;
    return -1;
}