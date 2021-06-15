#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "thread_pool.h"

struct thread_pool_s {
    unsigned int num_threads;
    pthread_t* tids;
};

/**
 * Create a thread pool
 * Spawn num_threads threads.
 * Each thread is created executing the worker function, with argument arg
 * Return a pointer to a thread_pool_t on success.
 * Return NULL on error, and errno is set appropriately.
*/
thread_pool_t* thread_pool_create(unsigned int num_threads,
    void* (*worker)(void*),
    void* arg)
{
    thread_pool_t* pool = malloc(sizeof(thread_pool_t));
    if (pool == NULL) {
        return NULL;
    }

    pthread_t* tids = malloc(num_threads * sizeof(pthread_t));
    if (tids == NULL) {
        return NULL;
    }
    pool->num_threads = num_threads;
    pool->tids = tids;

    for (unsigned int i = 0; i < num_threads; ++i) {
        int create_res = pthread_create(&(pool->tids[i]), NULL, worker, arg);
        if (create_res == -1) {
            return NULL;
        }
    }

    return pool;
}

/**
 * Join all the threads of the thread pool.
 * The pool struct is destroyed.
 * Return 0 on success.
 * Return -1 on error, and errno is set appropriately.
*/
int thread_pool_join(thread_pool_t* pool)
{
    if (pool == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (unsigned int i = 0; i < pool->num_threads; ++i) {
        int join_res = pthread_join(pool->tids[i], NULL);
        if (join_res == -1) {
            return -1;
        }
    }

    free(pool->tids);
    free(pool);
    return 0;
}