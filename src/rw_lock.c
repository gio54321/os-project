#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "rw_lock.h"

struct rw_lock_s {
    pthread_mutex_t mutex;
    pthread_cond_t read_go;
    pthread_cond_t write_go;

    int active_readers;
    int waiting_readers;

    int active_writers;
    int waiting_writers;
};

static bool readers_should_wait(rw_lock_t* lock)
{
    return lock->active_writers > 0 || lock->waiting_writers > 0;
}

static bool writer_should_wait(rw_lock_t* lock)
{
    return lock->active_readers > 0 || lock->active_writers > 0;
}

/**
 * Create a readers/writers lock
 * Return NULL on error and errno is set appropriately
*/
rw_lock_t* create_rw_lock()
{
    rw_lock_t* lock = malloc(sizeof(struct rw_lock_s));
    if (lock == NULL) {
        return NULL;
    }

    int init_res = pthread_mutex_init(&lock->mutex, NULL);
    if (init_res == -1) {
        return NULL;
    }

    init_res = pthread_cond_init(&lock->read_go, NULL);
    if (init_res == -1) {
        return NULL;
    }

    init_res = pthread_cond_init(&lock->write_go, NULL);
    if (init_res == -1) {
        return NULL;
    }

    lock->active_readers = 0;
    lock->active_writers = 0;
    lock->waiting_readers = 0;
    lock->waiting_writers = 0;
    return lock;
}

/**
 * Destroy a readers/writers lock
 * Return -1 on error and errno is set appropriately
*/
int destroy_rw_lock(rw_lock_t* lock)
{
    if (lock == NULL) {
        errno = EINVAL;
        return -1;
    }

    int destroy_res = pthread_mutex_destroy(&lock->mutex);
    if (destroy_res == -1) {
        return -1;
    }

    destroy_res = pthread_cond_destroy(&lock->read_go);
    if (destroy_res == -1) {
        return -1;
    }

    destroy_res = pthread_cond_destroy(&lock->write_go);
    if (destroy_res == -1) {
        return -1;
    }

    free(lock);
    return 0;
}

/**
 * Lock for reading. Critical section must end with read_unlock()
 * Return -1 on error and errno is set appropriately
*/
int read_lock(rw_lock_t* lock)
{
    if (lock == NULL) {
        errno = EINVAL;
        return -1;
    }

    int lock_res = pthread_mutex_lock(&lock->mutex);
    if (lock_res == -1) {
        return -1;
    }

    lock->waiting_readers++;

    while (readers_should_wait(lock)) {
        pthread_cond_wait(&lock->read_go, &lock->mutex);
    }

    lock->waiting_readers--;
    lock->active_readers++;

    int unlock_res = pthread_mutex_unlock(&lock->mutex);
    if (unlock_res == -1) {
        return -1;
    }

    return 0;
}

/**
 * Unlock for reading.
 * Return -1 on error and errno is set appropriately
*/
int read_unlock(rw_lock_t* lock)
{
    if (lock == NULL) {
        errno = EINVAL;
        return -1;
    }

    int lock_res = pthread_mutex_lock(&lock->mutex);
    if (lock_res == -1) {
        return -1;
    }

    lock->active_readers--;

    if (lock->active_readers == 0 && lock->waiting_writers > 0) {
        pthread_cond_signal(&lock->write_go);
    }

    int unlock_res = pthread_mutex_unlock(&lock->mutex);
    if (unlock_res == -1) {
        return -1;
    }

    return 0;
}

/**
 * Lock for writing. Critical section must end with write_unlock()
 * Return -1 on error and errno is set appropriately
*/
int write_lock(rw_lock_t* lock)
{
    if (lock == NULL) {
        errno = EINVAL;
        return -1;
    }

    int lock_res = pthread_mutex_lock(&lock->mutex);
    if (lock_res == -1) {
        return -1;
    }

    lock->waiting_writers++;

    while (writer_should_wait(lock)) {
        pthread_cond_wait(&lock->write_go, &lock->mutex);
    }

    lock->waiting_writers--;
    lock->active_writers++;

    int unlock_res = pthread_mutex_unlock(&lock->mutex);
    if (unlock_res == -1) {
        return -1;
    }

    return 0;
}

/**
 * Unlock for writing.
 * Return -1 on error and errno is set appropriately
*/
int write_unlock(rw_lock_t* lock)
{
    if (lock == NULL) {
        errno = EINVAL;
        return -1;
    }

    int lock_res = pthread_mutex_lock(&lock->mutex);
    if (lock_res == -1) {
        return -1;
    }

    lock->active_writers--;

    if (lock->waiting_writers > 0) {
        pthread_cond_signal(&lock->write_go);
    } else {
        pthread_cond_broadcast(&lock->read_go);
    }

    int unlock_res = pthread_mutex_unlock(&lock->mutex);
    if (unlock_res == -1) {
        return -1;
    }

    return 0;
}

void rw_lock_debug_assert_invariant(rw_lock_t* lock)
{
    // only for debug we assume that lock and unlock don't fail
    pthread_mutex_lock(&lock->mutex);
    //printf("active_readers:%d waiting_readers:%d active_writers:%d waiting_writers:%d\n",
    //lock->active_readers, lock->waiting_readers, lock->active_writers, lock->waiting_readers);
    assert(lock->active_writers <= 1);
    if (lock->active_writers > 0)
        assert(lock->active_readers == 0);
    if (lock->active_readers > 0)
        assert(lock->active_writers == 0);
    pthread_mutex_unlock(&lock->mutex);
}
