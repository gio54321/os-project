#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logger.h"
#include "unbounded_shared_buffer.h"

usbuf_t* buf;
void* producer(void* arg)
{
    for (int i = 0; i < 50; i++) {
        char* log_str = malloc(11 * sizeof(char));
        strcpy(log_str, "AAAAAAAAAA");
        assert(usbuf_put(buf, log_str) == 0);
    }
    return NULL;
}

void concurrent_test(unsigned int num_producers)
{
    buf = usbuf_create(FIFO_POLICY);
    pthread_t producers_tids[num_producers];
    pthread_t logger_tid;

    pthread_create(&logger_tid, NULL, logger_entry_point, buf);
    for (int i = 0; i < num_producers; ++i) {
        pthread_create(&producers_tids[i], NULL, producer, NULL);
    }
    for (int i = 0; i < num_producers; ++i) {
        pthread_join(producers_tids[i], NULL);
    }
    // since all the producers finished, close the buffer and wait
    // for the logger
    assert(usbuf_close(buf) == 0);
    pthread_join(logger_tid, NULL);
    assert(usbuf_free(buf) == 0);
}

int main(void)
{
    concurrent_test(2);
    concurrent_test(10);
    concurrent_test(30);
    return 0;
}