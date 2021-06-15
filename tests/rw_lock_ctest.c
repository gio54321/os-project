#include <assert.h>
#include <pthread.h>
#include <stdio.h>

#include "rw_lock.h"

rw_lock_t* lock;

void* worker(void* arg)
{
    for (int i = 0; i < 10; ++i) {
        write_lock(lock);
        rw_lock_debug_assert_invariant(lock);
        // printf("writing...\n");
        write_unlock(lock);
        rw_lock_debug_assert_invariant(lock);
        read_lock(lock);
        rw_lock_debug_assert_invariant(lock);
        // printf("reading...\n");
        read_unlock(lock);
        rw_lock_debug_assert_invariant(lock);
    }
    return NULL;
}

int main(void)
{
    lock = create_rw_lock();

    pthread_t tids[50];
    for (int i = 0; i < 50; i++) {
        pthread_create(&tids[i], NULL, worker, NULL);
    }
    for (int i = 0; i < 50; i++) {
        pthread_join(tids[i], NULL);
    }
    return 0;
}