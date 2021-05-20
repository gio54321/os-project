#include <errno.h>
#include <unistd.h>

#include "protocol.h"

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
        write_res = write(fd, &packet->op, 1);
        return write_res;

    case ACK:
        write_res = write(fd, &packet->op, 1);
        return write_res;

    case ERROR:
        write_res = write(fd, &packet->op, 1);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = write(fd, &packet->err_code, 1);
        return write_res;

    case DATA:
        write_res = write(fd, &packet->op, 1);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = write(fd, &packet->data_size, 8);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = write(fd, packet->data, packet->data_size);
        return write_res;

    case FILE_P:
        write_res = write(fd, &packet->op, 1);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = write(fd, &packet->name_length, 8);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = write(fd, packet->filename, packet->name_length);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = write(fd, &packet->data_size, 8);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = write(fd, packet->data, packet->data_size);
        return write_res;

    case FILE_SEQUENCE:
        write_res = write(fd, &packet->op, 1);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = write(fd, &packet->count, 8);
        return write_res;

    case OPEN_FILE:
        write_res = write(fd, &packet->op, 1);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = write(fd, &packet->name_length, 8);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = write(fd, packet->filename, packet->name_length);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = write(fd, &packet->flags, 1);
        return write_res;

    case CLOSE_FILE:
    case READ_FILE:
    case APPEND_TO_FILE:
    case LOCK_FILE:
    case UNLOCK_FILE:
        write_res = write(fd, &packet->op, 1);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = write(fd, &packet->name_length, 8);
        if (write_res <= 0) {
            return write_res;
        }
        write_res = write(fd, packet->filename, packet->name_length);
        return write_res;
    }
    return -1;
}

int receive_packet(int fd, struct packet* res_packet)
{
    if (res_packet == NULL) {
        errno = EINVAL;
        return -1;
    }

    ssize_t read_res = read(fd, &res_packet->op, 1);
    if (read_res <= 0) {
        return read_res;
    }

    switch (res_packet->op) {

    case NIL:
        errno = EINVAL;
        return -1;

    case COMP:
    case ACK:
        return read_res;

    case ERROR:
        read_res = read(fd, &res_packet->err_code, 1);
        return read_res;

    case DATA:
        read_res = read(fd, &res_packet->data_size, 8);
        if (read_res <= 0) {
            return read_res;
        }

        res_packet->data = malloc(res_packet->data_size);
        if (res_packet->data == NULL) {
            errno = ENOMEM;
            return -1;
        }
        read_res = read(fd, res_packet->data, res_packet->data_size);
        return read_res;

    case FILE_P:
        read_res = read(fd, &res_packet->name_length, 8);
        if (read_res <= 0) {
            return read_res;
        }
        res_packet->filename = malloc(res_packet->name_length + 1);
        if (res_packet->filename == NULL) {
            errno = ENOMEM;
            return -1;
        }
        read_res = read(fd, res_packet->filename, res_packet->name_length);
        if (read_res <= 0) {
            return read_res;
        }
        res_packet->filename[res_packet->name_length] = '\0';
        read_res = read(fd, &res_packet->data_size, 8);
        if (read_res <= 0) {
            return read_res;
        }
        res_packet->data = malloc(res_packet->data_size);
        if (res_packet->data == NULL) {
            free(res_packet->filename);
            errno = ENOMEM;
            return -1;
        }
        read_res = read(fd, res_packet->data, res_packet->data_size);
        return read_res;

    case FILE_SEQUENCE:
        read_res = read(fd, &res_packet->count, 8);
        return read_res;

    case OPEN_FILE:
        read_res = read(fd, &res_packet->name_length, 8);
        if (read_res <= 0) {
            return read_res;
        }
        res_packet->filename = malloc(res_packet->name_length + 1);
        if (res_packet->filename == NULL) {
            errno = ENOMEM;
            return -1;
        }
        read_res = read(fd, res_packet->filename, res_packet->name_length);
        if (read_res <= 0) {
            return read_res;
        }
        res_packet->filename[res_packet->name_length] = '\0';
        read_res = read(fd, &res_packet->flags, 1);
        return read_res;

    case CLOSE_FILE:
    case READ_FILE:
    case APPEND_TO_FILE:
    case LOCK_FILE:
    case UNLOCK_FILE:
        read_res = read(fd, &res_packet->name_length, 8);
        if (read_res <= 0) {
            return read_res;
        }
        res_packet->filename = malloc(res_packet->name_length + 1);
        if (res_packet->filename == NULL) {
            errno = ENOMEM;
            return -1;
        }
        res_packet->filename[res_packet->name_length] = '\0';
        read_res = read(fd, res_packet->filename, res_packet->name_length);
        return read_res;
    }
    return -1;
}