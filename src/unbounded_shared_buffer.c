#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "unbounded_shared_buffer.h"
#define INITIAL_CAPACITY 4

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
};

usbuf_t* usbuf_create(buf_policy policy)
{
    if (policy != FIFO_POLICY && policy != LIFO_POLICY) {
        return NULL;
    }

    usbuf_t* buf = malloc(sizeof(usbuf_t));
    if (buf == NULL) {
        return NULL;
    }

    buf->policy = policy;
    buf->start = NULL;
    buf->end = NULL;
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
    return buf;
}

int usbuf_put(usbuf_t* buf, void* item)
{
    int lock_res = pthread_mutex_lock(&buf->mutex);
    if (lock_res != 0) {
        return -1;
    }

    struct node* new_node = malloc(sizeof(struct node));
    if (new_node == NULL) {
        return -1;
    }

    new_node->data = item;

    if (buf->policy == FIFO_POLICY) {
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

void* usbuf_get(usbuf_t* buf)
{
    int lock_res = pthread_mutex_lock(&buf->mutex);
    if (lock_res != 0) {
        return NULL;
    }

    while (buf->start == NULL) {
        int wait_res = pthread_cond_wait(&buf->not_anymore_empty, &buf->mutex);
        if (wait_res != 0) {
            return NULL;
        }
    }
    void* res = buf->start->data;
    struct node* to_be_removed_node = buf->start;
    buf->start = buf->start->next;
    free(to_be_removed_node);
    int unlock_res = pthread_mutex_unlock(&buf->mutex);
    if (unlock_res != 0) {
        return NULL;
    }
    return res;
}

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