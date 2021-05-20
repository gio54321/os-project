#include <assert.h>
#include <stdio.h>

#include "int_queue.h"

int main()
{
    int_queue_t* queue = int_queue_create();
    assert(queue != NULL);

    assert(int_queue_put(queue, 5) == 0);
    assert(int_queue_put(queue, 6) == 0);
    assert(int_queue_put(queue, 7) == 0);
    assert(int_queue_put(queue, 42) == 0);

    int value;
    assert(int_queue_get(queue, &value) == 1);
    assert(value == 5);
    assert(int_queue_get(queue, &value) == 1);
    assert(value == 6);
    assert(int_queue_get(queue, &value) == 1);
    assert(value == 7);
    assert(int_queue_get(queue, &value) == 1);
    assert(value == 42);

    // empty queue
    assert(int_queue_get(queue, &value) == 0);
    assert(int_queue_get(queue, &value) == 0);

    assert(int_queue_free(queue) == 0);
    return 0;
}