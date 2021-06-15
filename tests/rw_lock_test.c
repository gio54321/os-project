#include <assert.h>
#include <stdio.h>

#include "rw_lock.h"

int main(void)
{
    rw_lock_t* lock = create_rw_lock();
    assert(lock != NULL);
    assert(read_lock(lock) == 0);
    rw_lock_debug_assert_invariant(lock);
    assert(read_unlock(lock) == 0);
    assert(write_lock(lock) == 0);
    rw_lock_debug_assert_invariant(lock);
    assert(write_unlock(lock) == 0);
    assert(destroy_rw_lock(lock) == 0);
    return 0;
}