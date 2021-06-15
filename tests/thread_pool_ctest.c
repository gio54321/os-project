#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "thread_pool.h"
#include "unbounded_shared_buffer.h"

void* worker(void* arg)
{
    usbuf_t* buf = arg;
    for (;;) {
        void* res;
        int get_res = usbuf_get(buf, &res);
        if (get_res == -2) {
            return NULL;
        }
        assert(get_res == 0);
        //printf("%d\n", *((int*)res));
        free(res);
    }
}

int main(void)
{
    usbuf_t* buf = usbuf_create(FIFO_POLICY);
    thread_pool_t* pool = thread_pool_create(10, worker, buf);

    for (int i = 0; i < 5000; ++i) {
        int* n = malloc(sizeof(int));
        *n = i;
        usbuf_put(buf, n);
    }

    usbuf_close(buf);

    thread_pool_join(pool);
    return 0;
}