#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "thread_pool.h"

void* worker(void* arg)
{
    int* int_arg = arg;
    assert(*int_arg == 42);
    return NULL;
}

int main(void)
{
    int* n = malloc(sizeof(int));
    *n = 42;
    thread_pool_t* pool = thread_pool_create(5, worker, n);

    thread_pool_join(pool);
    free(n);
    return 0;
}