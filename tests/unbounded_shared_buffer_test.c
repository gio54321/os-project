#include <assert.h>
#include <stdio.h>

#include "unbounded_shared_buffer.h"

int main(void)
{
    // tests basic policy properties
    int a, b, c, d;
    usbuf_t* buf = usbuf_create(FIFO_POLICY);
    usbuf_put(buf, &a);
    usbuf_put(buf, &b);
    usbuf_put(buf, &c);
    usbuf_put(buf, &d);
    assert(usbuf_get(buf) == &a);
    assert(usbuf_get(buf) == &b);
    assert(usbuf_get(buf) == &c);
    assert(usbuf_get(buf) == &d);
    usbuf_free(buf);

    buf = usbuf_create(LIFO_POLICY);
    usbuf_put(buf, &a);
    usbuf_put(buf, &b);
    usbuf_put(buf, &c);
    usbuf_put(buf, &d);
    assert(usbuf_get(buf) == &d);
    assert(usbuf_get(buf) == &c);
    assert(usbuf_get(buf) == &b);
    assert(usbuf_get(buf) == &a);
    usbuf_free(buf);

    //TODO: add concurrent tests
}