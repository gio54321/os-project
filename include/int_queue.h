#ifndef INT_QUEUE_H
#define INT_QUEUE_H

typedef struct int_queue_s int_queue_t;

//TODO add documentation
int_queue_t* int_queue_create();
int int_queue_put(int_queue_t* queue, int value);
int int_queue_get(int_queue_t* queue, int* value);
int int_queue_free(int_queue_t* queue);

#endif