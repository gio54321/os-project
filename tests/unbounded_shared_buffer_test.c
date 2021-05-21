#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "unbounded_shared_buffer.h"

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

    return 0;
}