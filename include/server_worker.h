#ifndef SERVER_WORKER_H
#define SERVER_WORKER_H

#include "file_storage_internal.h"
#include "unbounded_shared_buffer.h"

/**
 * Struct that defines what is passed to all the worker threads
*/
typedef struct worker_arg_s {
    usbuf_t* master_to_workers_buffer;
    int worker_to_master_pipe_write_fd;
    file_storage_t* file_storage;

    unsigned long max_num_files;
    unsigned long max_storage_size;
} worker_arg_t;

void* server_worker_entry_point(void* arg);
#endif