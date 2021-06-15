#ifndef RW_LOCK_H
#define RW_LOCK_H

typedef struct rw_lock_s rw_lock_t;

/**
 * Create a readers/writers lock
 * Return NULL on error and errno is set appropriately
*/
rw_lock_t* create_rw_lock();

/**
 * Destroy a readers/writers lock
 * Return -1 on error and errno is set appropriately
*/
int destroy_rw_lock(rw_lock_t* lock);

/**
 * Lock for reading. Critical section must end with read_unlock()
 * Return -1 on error and errno is set appropriately
*/
int read_lock(rw_lock_t* lock);

/**
 * Unlock for reading.
 * Return -1 on error and errno is set appropriately
*/
int read_unlock(rw_lock_t* lock);

/**
 * Lock for writing. Critical section must end with write_unlock()
 * Return -1 on error and errno is set appropriately
*/
int write_lock(rw_lock_t* lock);

/**
 * Unlock for writing.
 * Return -1 on error and errno is set appropriately
*/
int write_unlock(rw_lock_t* lock);

void rw_lock_debug_assert_invariant(rw_lock_t* lock);

#endif