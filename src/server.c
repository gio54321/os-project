#include <stdio.h>
#include <stdlib.h>

#include "file_storage_internal.h"
#include "server_worker.h"
#include "thread_pool.h"
#include "unbounded_shared_buffer.h"
#include "utils.h"

int main(int argc, char* argv[])
{
    int sig_handler_to_master_pipe[2];
    int workers_to_master_pipe[2];
    usbuf_t* master_to_workers_buffer;
    usbuf_t* logger_buffer;

    DIE_NULL(master_to_workers_buffer = usbuf_create(FIFO_POLICY), "usbuf create");
    DIE_NULL(logger_buffer = usbuf_create(FIFO_POLICY), "usbuf create");

    worker_arg_t* worker_arg = malloc(sizeof(worker_arg_t));
    worker_arg->master_to_workers_buffer = master_to_workers_buffer;
    thread_pool_t* workers_pool = thread_pool_create(10, server_worker_entry_point, worker_arg);
    usbuf_close(master_to_workers_buffer);
    thread_pool_join(workers_pool);

    free(worker_arg);
    usbuf_free(master_to_workers_buffer);
    usbuf_free(logger_buffer);

    return 0;
}