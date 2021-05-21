#ifndef UNBOUNDED_SHARED_BUFFER_H
#define UNBOUNDED_SHARED_BUFFER_H

typedef struct usbuf_s usbuf_t;
typedef enum {
    FIFO_POLICY,
    LIFO_POLICY
} buf_policy;

/**
 * Create an unbounded shared buffer with given policy.
 * The function returns NULL on error
 * (e.g. policy is neither FIFO_POLICY or LIFO_POLICY, allocation error etc.)
 * otherwise it returns a pointer to a valid usbuf_t.
 * The buffer will need to be freed with usbuf_free.
 * The buffer holds void pointers, to make it generic.
 * The user will need to take care of allocation/deallocation of such pointers.
 * usbuf_t shall be treated as opaque, so it shall be only accessed via the library
 * functions.
*/
usbuf_t* usbuf_create(buf_policy policy);

/**
 * Put an element in the shared buffer.
 * This operation is always non-blocking.
 * The function returns 0 on successful insertion, -2 on buffer closed,
 * otherwise it returns -1.
 * if the function returns -1 something very bad happened (e.g. the internal
 * mutex failed to lock/unlock) and the buffer shall be considered corrupted.
 * any operation after -1 is returned shall be considered U.B.
*/
int usbuf_put(usbuf_t* buf, void* item);

/**
 * Put an element in the shared buffer.
 * If the buffer is empty, then the calls blocks until there is an element available.
 * The function returns 0 on successful extraction, and the address of the item is
 * put in res, it returns -2 on buffer closed, otherwise it returns -1
 * if the function returns -2 it is guaranteed that the buffer is empty
 * if the function returns -1 something very bad happened (e.g. the internal
 * mutex failed to lock/unlock) and the buffer shall be considered corrupted.
 * any operation after NULL is returned shall be considered U.B.
*/
int usbuf_get(usbuf_t* buf, void** res);

/**
 * Close the buffer
 * any usbuf_put after the buffer is closed returns with a buffer closed error
 * wake up any eventual threads that are blocked on usbuf_get
 * every usbuf_get is valid until the buffer is not empty.
 * once the buffer becomes empty usbus_get returns a closed buffer error
 * this function can be safely called when the buffer is still shared between threads
 * the common pattern is to call usbuf_close when all the producers returned,
 * so the consumers will finish to extract all the elements from the buffer
 * and then they will return on buffer closed error
*/
int usbuf_close(usbuf_t* buf);

/**
 * free the shared buffer buf
 * if the buffer is non-empty, then the function returns -1 and the memory is not freed,
 * since the library doesn't know what to do with the void pointesr stored inside
 * if the buffer is empty, then the memory is freed.
 * any operation performed after 0 is returned shall be considered U.B.
 * this operation shall not be done when the buffer is still shared between threads
*/
int usbuf_free(usbuf_t* buf);

#endif