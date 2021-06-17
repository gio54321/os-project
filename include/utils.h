#ifndef UTILS_H
#define UTILS_H

#include <unistd.h>

#define DIE_NEG1(code, name) \
    if ((code) == -1) {      \
        perror(name);        \
        exit(EXIT_FAILURE);  \
    }

#define DIE_NEG(code, name) \
    if ((code) < 0) {       \
        perror(name);       \
        exit(EXIT_FAILURE); \
    }

#define DIE_NULL(code, name) \
    if ((code) == NULL) {    \
        perror(name);        \
        exit(EXIT_FAILURE);  \
    }

/**
 * Read n bytes from the file descriptor fd
 * this function returns the number of bytes read, but it is guaranteed that if
 * there is availability, it will read all the n bytes (avoiding partial reads)
 * Credits to R. Stevens et al.
*/
ssize_t readn(int fd, void* buf, size_t nbytes);

/**
 * Write n bytes from the file descriptor fd
 * this function returns the number of bytes writenn, but it is guaranteed that if
 * there is availability, it will write all the n bytes (avoiding partial writes)
 * Credits to R. Stevens et al.
*/
ssize_t writen(int fd, void* buf, size_t nbytes);

/**
 * Convert a string to a long
 * Return 0 on success and the resulting long is stored in n
 * Return -1 on failure
*/
int string_to_long(const char* s, long* n);

#endif