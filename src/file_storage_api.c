#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "file_storage_api.h"
#include "protocol.h"
#include "utils.h"

// global socket fd
int socket_fd = -1;

// global bool to enable prints
bool FILE_STORAGE_API_PRINTS_ENABLED = false;

#define PRINT_IF_EN(...)                   \
    if (FILE_STORAGE_API_PRINTS_ENABLED) { \
        printf(__VA_ARGS__);               \
    }

#define PRINT_ERR_CODE_IF_EN(err_code, context) \
    if (FILE_STORAGE_API_PRINTS_ENABLED) {      \
        print_error_code(err_code, context);    \
    }

static int receive_files_from_server(const char* dirname, const char* error_context)
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
            PRINT_ERR_CODE_IF_EN(response.err_code, error_context);
            errno = EBADE;
            return -1;
        }
        if (response.op == FILE_P) {
            if (dirname != NULL) {
                save_file_to_disk(dirname, response.filename, response.data_size, response.data);
                PRINT_IF_EN("received file %s, written to %s\n", response.filename, dirname);
            } else {
                PRINT_IF_EN("received file %s, ingored\n", response.filename);
            }
            destroy_packet(&response);
        }
    }
}

int openConnection(const char* sockname, int msec, const struct timespec abstime)
{
    PRINT_IF_EN("open the connection to %s\n", sockname);
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
        // wait msec milliseconds
        struct timespec t;
        t.tv_sec = 0;
        t.tv_nsec = msec * 1E6;
        nanosleep(&t, NULL);
        time_t curr_time = time(NULL);
        while (curr_time <= abstime.tv_sec) {
            PRINT_IF_EN("Connection to %s failed, retrying to connect...\n", sockname);
            connect_res = connect(socket_fd, (struct sockaddr*)&sa, sizeof(struct sockaddr_un));
            if (connect_res == 0) {
                return 0;
            }

            // wait msec milliseconds and the recalculate current time
            struct timespec t;
            t.tv_sec = 0;
            t.tv_nsec = msec * 1E6;
            nanosleep(&t, NULL);
            curr_time = time(NULL);
        }
        // it the function did not return earlier, then the
        // current time exceeded the abs time, so the function returns an error
        PRINT_IF_EN("Connection timed out, failed to connect\n");
        errno = ETIMEDOUT;
        return -1;
    } else {
        return 0;
    }
}

int closeConnection(const char* sockname)
{
    if (sockname == NULL) {
        errno = EINVAL;
        return -1;
    }
    PRINT_IF_EN("close the connection to %s\n", sockname);
    int close_res = close(socket_fd);
    return close_res;
}

int openFile(const char* pathname, int flags)
{
    if (pathname == NULL) {
        errno = EINVAL;
        return -1;
    }
    PRINT_IF_EN("open file %s with flag O_CREATE %d and O_LOCK %d\n", pathname, flags & O_CREATE, flags & O_LOCK);

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
        PRINT_ERR_CODE_IF_EN(response.err_code, "openFile");
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
    PRINT_IF_EN("read the file %s\n", pathname);

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

        PRINT_IF_EN("read %zd bytes of the file %s\n", response.data_size, pathname);
        return 0;
    }

    // if the response is an error then print it to stderr
    if (response.op == ERROR) {
        PRINT_ERR_CODE_IF_EN(response.err_code, "readFile");
    }
    errno = EBADE;
    return -1;
}

int readNFiles(int n, const char* dirname)
{
    PRINT_IF_EN("read %d files\n", n);

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

    return receive_files_from_server(dirname, "readNFiles");
}

int writeFile(const char* pathname, const char* dirname)
{
    if (pathname == NULL) {
        errno = EINVAL;
        return -1;
    }

    // open the file pointed by pathname
    FILE* f = fopen(pathname, "rb");

    // calculate the size of the file
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    void* buf = malloc(fsize);
    if (buf == NULL) {
        errno = ENOMEM;
        return -1;
    }
    fread(buf, sizeof(char), fsize, f);
    fclose(f);

    PRINT_IF_EN("write %zd bytes to the file %s\n", fsize, pathname);

    // send the request to the server
    struct packet request;
    clear_packet(&request);
    request.op = WRITE_FILE;
    request.data_size = fsize;
    request.data = buf;
    request.name_length = strlen(pathname);
    request.filename = malloc((strlen(pathname) + 1) * sizeof(char));
    if (request.filename == NULL) {
        errno = ENOMEM;
        free(buf);
        free(request.filename);
        return -1;
    }
    strcpy(request.filename, pathname);
    int send_res = send_packet(socket_fd, &request);
    if (send_res <= 0) {
        errno = EIO;
        free(buf);
        free(request.filename);
        return -1;
    }

    free(buf);
    free(request.filename);
    return receive_files_from_server(dirname, "writeFile");
}

int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname)
{
    if (pathname == NULL) {
        errno = EINVAL;
        return -1;
    }
    PRINT_IF_EN("append %zd bytes to the file %s\n", size, pathname);

    // send the request to the server
    struct packet request;
    clear_packet(&request);
    request.op = APPEND_TO_FILE;
    request.data_size = size;
    request.data = buf;
    request.name_length = strlen(pathname);
    request.filename = malloc((strlen(pathname) + 1) * sizeof(char));
    if (request.filename == NULL) {
        errno = ENOMEM;
        free(buf);
        return -1;
    }
    strcpy(request.filename, pathname);
    int send_res = send_packet(socket_fd, &request);
    if (send_res <= 0) {
        errno = EIO;
        return -1;
    }

    return receive_files_from_server(dirname, "writeFile");
}

int lockFile(const char* pathname)
{
    if (pathname == NULL) {
        errno = EINVAL;
        return -1;
    }
    PRINT_IF_EN("lock the file %s\n", pathname);
    // send the request to the server
    struct packet request;
    clear_packet(&request);
    request.op = LOCK_FILE;
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
        PRINT_ERR_CODE_IF_EN(response.err_code, "lockFile");
    }
    errno = EBADE;
    return -1;
}
int unlockFile(const char* pathname)
{
    if (pathname == NULL) {
        errno = EINVAL;
        return -1;
    }
    PRINT_IF_EN("unlock the file %s\n", pathname);
    // send the request to the server
    struct packet request;
    clear_packet(&request);
    request.op = UNLOCK_FILE;
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
        PRINT_ERR_CODE_IF_EN(response.err_code, "lockFile");
    }
    errno = EBADE;
    return -1;
}

int closeFile(const char* pathname)
{
    if (pathname == NULL) {
        errno = EINVAL;
        return -1;
    }
    PRINT_IF_EN("close the file %s\n", pathname);
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
        PRINT_ERR_CODE_IF_EN(response.err_code, "closeFile");
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
    PRINT_IF_EN("remove the file %s\n", pathname);
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
        PRINT_ERR_CODE_IF_EN(response.err_code, "removeFile");
    }
    errno = EBADE;
    return -1;
}