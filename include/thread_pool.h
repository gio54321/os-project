#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "unbounded_shared_buffer.h"

typedef struct thread_pool_s thread_pool_t;

/**
 * Create a thread pool
 * Spawn num_threads threads.
 * Each thread is created executing the worker function, with argument arg
 * Return a pointer to a thread_pool_t on success.
 * Return NULL on error, and errno is set appropriately.
*/
thread_pool_t* thread_pool_create(unsigned int num_threads,
    void* (*worker)(void*),
    void* arg);

/**
 * Join all the threads of the thread pool.
 * The pool struct is destroyed.
 * Return 0 on success.
 * Return -1 on error, and errno is set appropriately.
*/
int thread_pool_join(thread_pool_t* pool);

#endif