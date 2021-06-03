#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "unbounded_shared_buffer.h"

struct node {
    void* data;
    struct node* next;
};

struct usbuf_s {
    struct node* start;
    struct node* end;
    buf_policy policy;
    pthread_mutex_t mutex;
    pthread_cond_t not_anymore_empty;
    int closed;
};

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
usbuf_t* usbuf_create(buf_policy policy)
{
    // policy has to be FIFO_POLICY or LIFO_POLICY
    if (policy != FIFO_POLICY && policy != LIFO_POLICY) {
        return NULL;
    }

    // allocate the new buffer
    usbuf_t* buf = malloc(sizeof(usbuf_t));
    if (buf == NULL) {
        return NULL;
    }

    // initializa all the fields of the struct
    buf->policy = policy;
    buf->start = NULL;
    buf->end = NULL;
    buf->closed = 0;
    int init_res = pthread_mutex_init(&buf->mutex, NULL);
    if (init_res != 0) {
        free(buf);
        return NULL;
    }

    init_res = pthread_cond_init(&buf->not_anymore_empty, NULL);
    if (init_res != 0) {
        pthread_mutex_destroy(&buf->mutex);
        free(buf);
        return NULL;
    }

    //finally return the pointer to the buffer to the caller
    return buf;
}

/**
 * Put an element in the shared buffer.
 * This operation is always non-blocking.
 * The function returns 0 on successful insertion, -2 on buffer closed,
 * otherwise it returns -1.
 * if the function returns -1 something very bad happened (e.g. the internal
 * mutex failed to lock/unlock) and the buffer shall be considered corrupted.
 * any operation after -1 is returned shall be considered U.B.
*/
int usbuf_put(usbuf_t* buf, void* item)
{
    int lock_res = pthread_mutex_lock(&buf->mutex);
    if (lock_res != 0) {
        return -1;
    }

    if (buf->closed) {
        int unlock_res = pthread_mutex_unlock(&buf->mutex);
        if (unlock_res != 0) {
            return -1;
        }
        return -2;
    }
    // create the new node
    struct node* new_node = malloc(sizeof(struct node));
    if (new_node == NULL) {
        return -1;
    }

    // put the data in the newly created node
    new_node->data = item;

    // if the policy is FIFO, then the node shall be inserted at the end of the
    // linked list, otherwise the node shall be inserted at the start of the
    // linked list
    if (buf->policy == FIFO_POLICY) {
        // insert node at the end of the linked list
        new_node->next = NULL;
        if (buf->end == NULL) {
            buf->start = new_node;
            buf->end = new_node;
        } else {
            buf->end->next = new_node;
            buf->end = new_node;
        }

    } else {
        // insert a the start of the linked list
        new_node->next = buf->start;
        buf->start = new_node;
        if (buf->end == NULL) {
            buf->end = new_node;
        }
    }

    // signal to one threads that was waiting for the buffer to become
    // non-empty
    int signal_res = pthread_cond_signal(&buf->not_anymore_empty);
    if (signal_res != 0) {
        return -1;
    }

    int unlock_res = pthread_mutex_unlock(&buf->mutex);
    if (unlock_res != 0) {
        return -1;
    }

    return 0;
}

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
int usbuf_get(usbuf_t* buf, void** res)
{
    int lock_res = pthread_mutex_lock(&buf->mutex);
    if (lock_res != 0) {
        return -1;
    }

    if (buf->closed && buf->start == NULL) {
        int unlock_res = pthread_mutex_unlock(&buf->mutex);
        if (unlock_res != 0) {
            return -1;
        }
        return -2;
    }

    // wait for the buffer to become non-empty
    while (buf->start == NULL) {
        int wait_res = pthread_cond_wait(&buf->not_anymore_empty, &buf->mutex);
        if (wait_res != 0) {
            return -1;
        }
        if (buf->closed && buf->start == NULL) {
            int unlock_res = pthread_mutex_unlock(&buf->mutex);
            if (unlock_res != 0) {
                return -1;
            }
            return -2;
        }
    }

    // remove the stating node
    *res = buf->start->data;
    struct node* to_be_removed_node = buf->start;
    buf->start = buf->start->next;
    if (buf->start == NULL) {
        buf->end = NULL;
    }
    free(to_be_removed_node);

    int unlock_res = pthread_mutex_unlock(&buf->mutex);
    if (unlock_res != 0) {
        return -1;
    }
    return 0;
}

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
int usbuf_close(usbuf_t* buf)
{
    int lock_res = pthread_mutex_lock(&buf->mutex);
    if (lock_res != 0) {
        return -1;
    }

    buf->closed = 1;

    int broadcast_res = pthread_cond_broadcast(&buf->not_anymore_empty);
    if (broadcast_res == -1) {
        return -1;
    }

    int unlock_res = pthread_mutex_unlock(&buf->mutex);
    if (unlock_res != 0) {
        return -1;
    }
    return 0;
}

/**
 * free the shared buffer buf
 * if the buffer is non-empty, then the function returns -1 and the memory is not freed,
 * since the library doesn't know what to do with the void pointesr stored inside
 * if the buffer is empty, then the memory is freed.
 * any operation performed after 0 is returned shall be considered U.B.
 * this operation shall not be done when the buffer is still shared between threads
*/
int usbuf_free(usbuf_t* buf)
{
    if (buf->start != NULL) {
        return -1;
    }
    pthread_cond_destroy(&buf->not_anymore_empty);
    pthread_mutex_destroy(&buf->mutex);
    free(buf);
    return 0;
}