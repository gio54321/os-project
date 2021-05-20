#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "int_queue.h"

struct node {
    int data;
    struct node* next;
};

struct int_queue_s {
    struct node* start;
    struct node* end;
};

int_queue_t* int_queue_create()
{
    int_queue_t* res = malloc(sizeof(int_queue_t));
    if (res == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    res->start = NULL;
    res->end = NULL;
    return res;
}

int int_queue_put(int_queue_t* queue, int value)
{
    if (queue == NULL) {
        errno = EINVAL;
        return -1;
    }
    struct node* new_node = malloc(sizeof(struct node));
    if (new_node == NULL) {
        errno = ENOMEM;
        return -1;
    }
    new_node->data = value;
    new_node->next = NULL;
    if (queue->end == NULL) {
        queue->start = new_node;
    } else {
        queue->end->next = new_node;
    }
    queue->end = new_node;
    return 0;
}

int int_queue_get(int_queue_t* queue, int* value)
{
    if (queue == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (queue->start == NULL) {
        return 0;
    }

    *value = queue->start->data;
    struct node* rm_node = queue->start;
    queue->start = queue->start->next;
    free(rm_node);
    return 1;
}

int int_queue_free(int_queue_t* queue)
{
    if (queue == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (struct node* n = queue->start; n != NULL;) {
        struct node* tmp = n;
        n = n->next;
        free(tmp);
    }
    free(queue);
    return 0;
}
