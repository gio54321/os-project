#ifndef UNBOUNDED_SHARED_BUFFER_H
#define UNBOUNDED_SHARED_BUFFER_H

typedef struct usbuf_s usbuf_t;
typedef enum {
    FIFO_POLICY,
    LIFO_POLICY
} buf_policy;

usbuf_t* usbuf_create(buf_policy policy);
int usbuf_put(usbuf_t* buf, void* item);
void* usbuf_get(usbuf_t* buf);
int usbuf_free(usbuf_t* buf);

#endif