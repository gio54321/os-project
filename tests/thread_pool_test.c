#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "thread_pool.h"

void* worker(void* arg)
{
    thread_pool_arg_t* pool_arg = arg;

    int* int_arg = pool_arg->common_arg;
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