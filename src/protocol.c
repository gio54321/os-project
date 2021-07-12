#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "protocol.h"
#include "utils.h"

/**
 * Clear the packet pointed by packet.
 * All the fields are cleared and initialized to 0
 * Returns -1 on error and errno is set appropriately
*/
int clear_packet(struct packet* packet)
{
    if (packet == NULL) {
        errno = EINVAL;
        return -1;
    }
    packet->op = NIL;
    packet->data = NULL;
    packet->data_size = 0;
    packet->name_length = 0;
    packet->filename = NULL;
    packet->err_code = 0;
    packet->flags = 0;
    packet->count = 0;
    return 0;
}

/**
 * Destroy the packet.
 * This shall be called only on a packet in which all the pointers either point
 * to a vaild location on the heap or are NULL. If the packet's fields contain
 * garbage pointers calling this function is U.B and might lead to segfault.
*/
int destroy_packet(struct packet* packet)
{
    if (packet == NULL) {
        errno = EINVAL;
        return -1;
    }
    // this works because free(NULL) is specified to be a NO-OP
    free(packet->data);
    free(packet->filename);
    return 0;
}

/**
 * Send a packet through fd
 * The information is contained in packet. The packet type is deduced by
 * packet->op. The caller must ensure that the packet structure contains all the
 * necessary information and that all the fields required by the kind of the
 * packet are meaningful. The calle may refer to the specification to see how the
 * packet are formed.
 * This function does not modify the packet.
 * Return -1 on error and errno is set appropriately, returns 0 on fd closed,
 * returns a positive value on success.
*/
ssize_t send_packet(int fd, struct packet* packet)
{
    ssize_t write_res;
    if (packet == NULL) {
        errno = EINVAL;
        return -1;
    }
    switch (packet->op) {

    case NIL:
        errno = EINVAL;
        return -1;

    case COMP:
        write_res = writen(fd, &packet->op, 1);
        return write_res;

    case ERROR:
        write_res = writen(fd, &packet->op, 1);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = writen(fd, &packet->err_code, 1);
        return write_res;

    case DATA:
        write_res = writen(fd, &packet->op, 1);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = writen(fd, &packet->data_size, 8);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = writen(fd, packet->data, packet->data_size);
        return write_res;

    case APPEND_TO_FILE:
    case WRITE_FILE:
    case FILE_P:
        write_res = writen(fd, &packet->op, 1);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = writen(fd, &packet->name_length, 8);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = writen(fd, packet->filename, packet->name_length);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = writen(fd, &packet->data_size, 8);
        if (write_res <= 0) {
            return write_res;
        }
        if (packet->data_size > 0) {
            write_res = writen(fd, packet->data, packet->data_size);
        }
        return write_res;

    case READ_N_FILES:
        write_res = writen(fd, &packet->op, 1);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = writen(fd, &packet->count, 8);
        return write_res;

    case OPEN_FILE:
        write_res = writen(fd, &packet->op, 1);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = writen(fd, &packet->name_length, 8);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = writen(fd, packet->filename, packet->name_length);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = writen(fd, &packet->flags, 1);
        return write_res;

    case CLOSE_FILE:
    case READ_FILE:
    case LOCK_FILE:
    case UNLOCK_FILE:
    case REMOVE_FILE:
        write_res = writen(fd, &packet->op, 1);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = writen(fd, &packet->name_length, 8);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = writen(fd, packet->filename, packet->name_length);
        return write_res;
    }
    return -1;
}

/**
 * Receive a packet through fd
 * The information is returned in res_packet, which shall point to a valid packet
 * structure location. Memory is allocated as needed, so the packet shall be destroyed
 * using destroy_packet. It is recommended to clear the packet before receiving data,
 * but it is not strictly required. See destroy_package description to get a full
 * explaination of this.
 * Return -1 on error and errno is set appropriately, returns 0 on fd closed,
 * returns a positive value on success.
*/
int receive_packet(int fd, struct packet* res_packet)
{
    if (res_packet == NULL) {
        errno = EINVAL;
        return -1;
    }

    ssize_t read_res = readn(fd, &res_packet->op, 1);
    if (read_res <= 0) {
        return read_res;
    }

    switch (res_packet->op) {

    case NIL:
        errno = EINVAL;
        return -1;

    case COMP:
        return read_res;

    case ERROR:
        read_res = readn(fd, &res_packet->err_code, 1);
        return read_res;

    case DATA:
        read_res = readn(fd, &res_packet->data_size, 8);
        if (read_res <= 0) {
            return read_res;
        }

        res_packet->data = malloc(res_packet->data_size);
        if (res_packet->data == NULL) {
            errno = ENOMEM;
            return -1;
        }
        read_res = readn(fd, res_packet->data, res_packet->data_size);
        return read_res;

    case APPEND_TO_FILE:
    case WRITE_FILE:
    case FILE_P:
        read_res = readn(fd, &res_packet->name_length, 8);
        if (read_res <= 0) {
            return read_res;
        }
        res_packet->filename = malloc(res_packet->name_length + 1);
        if (res_packet->filename == NULL) {
            errno = ENOMEM;
            return -1;
        }
        read_res = readn(fd, res_packet->filename, res_packet->name_length);
        if (read_res <= 0) {
            return read_res;
        }
        res_packet->filename[res_packet->name_length] = '\0';
        read_res = readn(fd, &res_packet->data_size, 8);
        if (read_res <= 0) {
            return read_res;
        }
        if (res_packet->data_size > 0) {
            res_packet->data = malloc(res_packet->data_size);
            if (res_packet->data == NULL) {
                free(res_packet->filename);
                errno = ENOMEM;
                return -1;
            }
            read_res = readn(fd, res_packet->data, res_packet->data_size);
        }
        return read_res;

    case READ_N_FILES:
        read_res = readn(fd, &res_packet->count, 8);
        return read_res;

    case OPEN_FILE:
        read_res = readn(fd, &res_packet->name_length, 8);
        if (read_res <= 0) {
            return read_res;
        }
        res_packet->filename = malloc(res_packet->name_length + 1);
        if (res_packet->filename == NULL) {
            errno = ENOMEM;
            return -1;
        }
        read_res = readn(fd, res_packet->filename, res_packet->name_length);
        if (read_res <= 0) {
            return read_res;
        }
        res_packet->filename[res_packet->name_length] = '\0';
        read_res = readn(fd, &res_packet->flags, 1);
        return read_res;

    case CLOSE_FILE:
    case READ_FILE:
    case LOCK_FILE:
    case UNLOCK_FILE:
    case REMOVE_FILE:
        read_res = readn(fd, &res_packet->name_length, 8);
        if (read_res <= 0) {
            return read_res;
        }
        res_packet->filename = malloc(res_packet->name_length + 1);
        if (res_packet->filename == NULL) {
            errno = ENOMEM;
            return -1;
        }
        res_packet->filename[res_packet->name_length] = '\0';
        read_res = readn(fd, res_packet->filename, res_packet->name_length);
        return read_res;
    }
    return -1;
}

/**
 * Print a human readable error on stderr
*/
void print_error_code(char error_code, const char* context)
{
    switch (error_code) {
    case FILE_ALREADY_EXISTS:
        fprintf(stderr, "%s: file already exists\n", context);
        break;
    case FILE_DOES_NOT_EXIST:
        fprintf(stderr, "%s: file does not exist\n", context);
        break;
    case FILE_ALREADY_LOCKED:
        fprintf(stderr, "%s: file is already locked\n", context);
        break;
    case FILE_IS_LOCKED_BY_ANOTHER_CLIENT:
        fprintf(stderr, "%s: file is locked by another client\n", context);
        break;
    case FILE_IS_NOT_OPENED:
        fprintf(stderr, "%s: file is not opened by the client\n", context);
        break;
    case FILE_WAS_ALREADY_WRITTEN:
        fprintf(stderr, "%s: file was already written\n", context);
        break;
    case FILE_IS_TOO_BIG:
        fprintf(stderr, "%s: file is too big\n", context);
        break;
    case FILE_IS_NOT_LOCKED:
        fprintf(stderr, "%s: file is not locked\n", context);
        break;
    default:
        fprintf(stderr, "%s: invalid error code\n", context);
        break;
    }
}