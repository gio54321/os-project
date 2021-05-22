#include <unistd.h>

#include "utils.h"

/**
 * Read n bytes from the file descriptor fd
 * this function returns the number of bytes read, but it is guaranteed that if
 * there is availability, it will read all the n bytes (avoiding partial reads)
 * Credits to R. Stevens et al.
*/
ssize_t readn(int fd, void* ptr, size_t n)
{
    size_t nleft;
    ssize_t nread;

    nleft = n;
    while (nleft > 0) {
        if ((nread = read(fd, ptr, nleft)) < 0) {
            if (nleft == n)
                return -1;
            else
                break;
        } else if (nread == 0)
            break;
        nleft -= nread;
        ptr += nread;
    }
    return (n - nleft);
}

/**
 * Write n bytes from the file descriptor fd
 * this function returns the number of bytes writenn, but it is guaranteed that if
 * there is availability, it will write all the n bytes (avoiding partial writes)
 * Credits to R. Stevens et al.
*/
ssize_t writen(int fd, void* ptr, size_t n)
{
    size_t nleft;
    ssize_t nwritten;

    nleft = n;
    while (nleft > 0) {
        if ((nwritten = write(fd, ptr, nleft)) < 0) {
            if (nleft == n)
                return -1;
            else
                break;
        } else if (nwritten == 0)
            break;
        nleft -= nwritten;
        ptr += nwritten;
    }
    return (n - nleft);
}