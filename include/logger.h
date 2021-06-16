#ifndef LOGGER_H
#define LOGGER_H

/**
 * Logger thread entry point
 * arg must be a pointer to an unbonded shared buffer allocated on the heap
 * the logger will get strings allocated on the heap from the buffer and will
 * put them into the logger.
 * The logger will take care of freeing those strings.
 * The logger terminates when the buffer is closed.
 * The logger always returns NULL
*/
void* logger_entry_point(void* arg);

/**
 * Log macro for easy formatted logging
*/
#define LOG(log_buf, ...)                                         \
    {                                                             \
        char* log_str;                                            \
        DIE_NULL(log_str = malloc(256 * sizeof(char)), "malloc"); \
        sprintf(log_str, __VA_ARGS__);                            \
        usbuf_put(log_buf, log_str);                              \
    }

#endif