#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "unbounded_shared_buffer.h"

#define DO_CONCURRENCY_TESTS 0

unsigned int num_items;
usbuf_t* buf;

void* producer(void* arg)
{
    unsigned int rand_state = time(NULL);
    int t = rand_r(&rand_state);
    for (int i = 0; i < num_items; ++i) {
        assert(usbuf_put(buf, &num_items) == 0);
        usleep(((double)t / RAND_MAX) * 1000);
    }
    return NULL;
}

void* consumer(void* arg)
{
    unsigned int rand_state = time(NULL);
    int t = rand_r(&rand_state);
    for (;;) {
        void* res = NULL;
        int get_res = usbuf_get(buf, &res);
        assert(get_res == 0 || get_res == -2);
        if (get_res == -2) {
            // buffer is closed, return
            break;
        }
        assert(res == &num_items);
        usleep(((double)t / RAND_MAX) * 1000);
    }
    return NULL;
}

void concurrent_test(unsigned int num_producers, unsigned int num_consumers, unsigned int num, buf_policy policy)
{
    num_items = num;
    buf = usbuf_create(policy);
    pthread_t producers_tids[num_producers];
    pthread_t consumers_tids[num_consumers];
    for (int i = 0; i < num_producers; ++i) {
        pthread_create(&producers_tids[i], NULL, producer, NULL);
    }
    for (int i = 0; i < num_consumers; ++i) {
        pthread_create(&consumers_tids[i], NULL, consumer, NULL);
    }
    for (int i = 0; i < num_producers; ++i) {
        pthread_join(producers_tids[i], NULL);
    }
    // since all the producers finished, close the buffer and wait
    // for all the consumers
    assert(usbuf_close(buf) == 0);
    for (int i = 0; i < num_consumers; ++i) {
        pthread_join(consumers_tids[i], NULL);
    }
    assert(usbuf_free(buf) == 0);
}

int main(void)
{
    // tests basic policy properties
    int a, b, c, d;
    usbuf_t* local_buf = usbuf_create(FIFO_POLICY);
    usbuf_put(local_buf, &a);
    usbuf_put(local_buf, &b);
    usbuf_put(local_buf, &c);
    usbuf_put(local_buf, &d);
    void* res = NULL;
    usbuf_get(local_buf, &res);
    assert(res == &a);
    usbuf_get(local_buf, &res);
    assert(res == &b);
    usbuf_get(local_buf, &res);
    assert(res == &c);
    usbuf_get(local_buf, &res);
    assert(res == &d);
    usbuf_free(local_buf);

    local_buf = usbuf_create(LIFO_POLICY);
    usbuf_put(local_buf, &a);
    usbuf_put(local_buf, &b);
    usbuf_put(local_buf, &c);
    usbuf_put(local_buf, &d);
    res = NULL;
    usbuf_get(local_buf, &res);
    assert(res == &d);
    usbuf_get(local_buf, &res);
    assert(res == &c);
    usbuf_get(local_buf, &res);
    assert(res == &b);
    usbuf_get(local_buf, &res);
    assert(res == &a);
    usbuf_free(local_buf);

    if (DO_CONCURRENCY_TESTS) {
        printf("running concurrency tests (this may take a while)...\n");
        concurrent_test(1, 1, 50, LIFO_POLICY);
        concurrent_test(2, 2, 50, LIFO_POLICY);
        concurrent_test(5, 5, 50, LIFO_POLICY);
        concurrent_test(30, 30, 50, LIFO_POLICY);
        concurrent_test(30, 5, 50, LIFO_POLICY);
        concurrent_test(5, 30, 50, LIFO_POLICY);

        concurrent_test(1, 1, 50, FIFO_POLICY);
        concurrent_test(2, 2, 50, FIFO_POLICY);
        concurrent_test(5, 5, 50, FIFO_POLICY);
        concurrent_test(30, 30, 50, FIFO_POLICY);
        concurrent_test(30, 5, 50, FIFO_POLICY);
        concurrent_test(5, 30, 50, FIFO_POLICY);
    }
}