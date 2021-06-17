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
    // TODO check sockname
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
    request.filename = pathname;
    request.flags = flags;
    int send_res = send_packet(socket_fd, &request);
    if (send_res <= 0) {
        errno = EIO;
        return -1;
    }

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
    request.filename = pathname;
    int send_res = send_packet(socket_fd, &request);
    if (send_res <= 0) {
        errno = EIO;
        return -1;
    }

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
int readNFiles(int n, const char* dirname);
int writeFile(const char* pathname, const char* dirname);
int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname);

int lockFile(const char* pathname);
int unlockFile(const char* pathname);

int closeFile(const char* pathname);
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
    request.filename = pathname;
    int send_res = send_packet(socket_fd, &request);
    if (send_res <= 0) {
        errno = EIO;
        return -1;
    }

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