/*
 * Protocol implementation
 * See docs/protocol_specification.txt for the specification 
*/

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdlib.h>

enum opcodes {
    NIL, // <- for representing an invalid packet
    COMP,
    ACK,
    ERROR,
    CLOSE_CONNECTION,
    DATA,
    FILE_P,
    FILE_SEQUENCE,
    OPEN_FILE,
    CLOSE_FILE,
    WRITE_FILE,
    READ_FILE,
    READ_N_FILES,
    APPEND_TO_FILE,
    LOCK_FILE,
    UNLOCK_FILE,
    REMOVE_FILE
};

enum err_codes {
    FILE_ALREADY_EXISTS,
    FILE_DOES_NOT_EXIST,
    FILE_ALREADY_LOCKED,
    FILE_IS_LOCKED_BY_ANOTHER_CLIENT
};

enum flags {
    O_CREATE = 1, //0b01
    O_LOCK = 2 //0b10
};

struct packet {
    char op;
    char err_code;
    u_int64_t name_length;
    char* filename;
    u_int64_t data_size;
    void* data;
    char flags;
    u_int64_t count;
};

/**
 * Clear the packet pointed by packet.
 * All the fields are cleared and initialized to 0
 * Returns -1 on error and errno is set appropriately
*/
int clear_packet(struct packet* packet);

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
ssize_t send_packet(int fd, struct packet* packet);

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
int receive_packet(int fd, struct packet* res_packet);

/**
 * Destroy the packet.
 * This shall be called only on a packet in which all the pointers either point
 * to a vaild location on the heap or are NULL. If the packet's fields contain
 * garbage pointers calling this function is U.B and might lead to segfault.
*/
int destroy_packet(struct packet* packet);

/**
 * Print a human readable error on stderr
*/
void print_error_code(char error_code, const char* context);
#endif