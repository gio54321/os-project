#include <stdlib.h>

#include "file_storage_internal.h"
#include "server_worker.h"
#include "thread_pool.h"
#include "unbounded_shared_buffer.h"
#include "utils.h"

void server_worker(unsigned int num_worker, worker_arg_t* worker_args)
{
}

void* server_worker_entry_point(void* arg)
{
    // unpack all the arguments and cast them to the correct type
    thread_pool_arg_t* pool_arg = arg;
    worker_arg_t* worker_arg = pool_arg->common_arg;

    // call the actual worker
    server_worker(pool_arg->num_worker, worker_arg);
    return NULL;
}