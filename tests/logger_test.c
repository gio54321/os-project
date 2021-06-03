#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logger.h"
#include "unbounded_shared_buffer.h"

usbuf_t* buf;

int main(void)
{
    buf = usbuf_create(FIFO_POLICY);
    pthread_t logger_tid;

    pthread_create(&logger_tid, NULL, logger_entry_point, buf);

    for (int i = 0; i < 50; i++) {
        char* log_str = malloc(11 * sizeof(char));
        assert(log_str != NULL);
        strcpy(log_str, "AAAAAAAAAA");
        assert(usbuf_put(buf, log_str) == 0);
    }
    assert(usbuf_close(buf) == 0);
    pthread_join(logger_tid, NULL);
    assert(usbuf_free(buf) == 0);
    return 0;
}