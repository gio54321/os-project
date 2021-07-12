#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "unbounded_shared_buffer.h"
#include "utils.h"

static int logger(usbuf_t* buf)
{
    // open the log file
    FILE* fd;
    DIE_NULL(fd = fopen("log.txt", "w+"), "log.txt file fopen");

    for (;;) {
        // wait for the producers to put in the buffer a string
        void* void_result;
        int get_res;
        DIE_NEG1(get_res = usbuf_get(buf, &void_result), "usbuf_get");
        // if the buffer is closed, then break out of the loop and terminate
        if (get_res == -2) {
            break;
        }
        char* result = (char*)void_result;

        // get the current time
        time_t current_time;
        DIE_NEG1(current_time = time(NULL), "log time");

        // convert the current time expressed by a human readable string
        // ctime_r is the thread-safe version of ctime
        char* c_time_string;
        char time_buf[100];
        DIE_NULL(c_time_string = ctime_r(&current_time, time_buf), "log ctime");

        time_buf[strnlen(time_buf, 100) - 1] = '\0';

        // print to the log the timestamp and message
        DIE_NEG(fprintf(fd, "[%s] %s\n", time_buf, result), "log fprintf");

        // free the allocated string
        free(result);
    }

    DIE_NEG1(fclose(fd), "log fclose");
    return 0;
}

/**
 * Logger thread entry point
 * arg must be a pointer to an unbonded shared buffer allocated on the heap
 * the logger will get strings allocated on the heap from the buffer and will
 * put them into the logger.
 * The logger will take care of freeing those strings.
 * The logger terminates when the buffer is closed.
 * The logger always returns NULL
*/
void* logger_entry_point(void* arg)
{
    usbuf_t* buf = arg;
    logger(buf);
    return NULL;
}