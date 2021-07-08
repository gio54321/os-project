#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

/**
 * Convert a string to a long
 * Return 0 on success and the resulting long is stored in n
 * Return -1 on failure
*/
int string_to_long(const char* s, long* n)
{
    if (s == NULL)
        return -1;
    if (strlen(s) == 0)
        return -1;
    char* e = NULL;
    errno = 0;
    long val = strtol(s, &e, 10);
    if (errno == ERANGE)
        return -1; // overflow
    if (e != NULL && *e == (char)0) {
        *n = val;
        return 0; // successo
    }
    return -1; // non e' un numero
}

// recursively create directories to form a pathname
// return 0 on success, -1 on failed creation
// dir is modified but if the function returns 0 then
// it is left as the original content
static int mkdir_recursive(const char* dir)
{
    char tmp[512];
    char* p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            int mkdir_res = mkdir(tmp, S_IRWXU);
            if (mkdir_res == -1) {
                if (errno != EEXIST) {
                    perror("mkdir");
                    return -1;
                }
            }
            *p = '/';
        }
    int mkdir_res = mkdir(tmp, S_IRWXU);
    if (mkdir_res == -1) {
        if (errno != EEXIST) {
            perror("mkdir");
            return -1;
        }
    }
    return 0;
}

// save file filename to dirname
// with content buf and size size
// creates the containing directories recursively
// return 0 on success and -1 on error
int save_file_to_disk(char* dirname, char* filename, size_t size, void* buf)
{
    if (size > 0) {
        // calculate the absolute path
        size_t dirname_len = strlen(dirname);
        char* abs_path = malloc((dirname_len + strlen(filename) + 2) * sizeof(char));
        strcpy(abs_path, dirname);
        abs_path[dirname_len] = '/';
        strcpy(abs_path + dirname_len + 1, filename + (filename[0] == '/' ? 1 : 0));

        // calculate the last slash position
        size_t last_slash_pos = -1;
        for (size_t i = strlen(abs_path) - 1; i >= 0; --i) {
            if (abs_path[i] == '/') {
                last_slash_pos = i;
                break;
            }
        }

        // recursively create directories to write the file
        if (last_slash_pos != -1) {
            abs_path[last_slash_pos] = '\0';
            struct stat file_stat;
            if (stat(abs_path, &file_stat) == -1) {
                int mkdir_res = mkdir_recursive(abs_path);
                if (mkdir_res == -1) {
                    return -1;
                }
            }
            abs_path[last_slash_pos] = '/';
        }

        // write the file to disk
        FILE* fd;
        fd = fopen(abs_path, "w+");
        if (fd == NULL) {
            fprintf(stderr, "Error: cannot create file %s\n", abs_path);
            return -1;
        }

        fwrite(buf, sizeof(char), size, fd);
        fclose(fd);

        free(abs_path);
    }
}