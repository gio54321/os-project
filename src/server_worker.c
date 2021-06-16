#include <stdio.h>
#include <stdlib.h>

#include "file_storage_internal.h"
#include "protocol.h"
#include "server_worker.h"
#include "thread_pool.h"
#include "unbounded_shared_buffer.h"
#include "utils.h"

static int client_cleanup(file_storage_t* file_storage, int client_fd)
{
    return 0;
}

static void server_worker(unsigned int num_worker, worker_arg_t* worker_args)
{
    usbuf_t* master_to_workers_buf = worker_args->master_to_workers_buffer;
    file_storage_t* file_storage = worker_args->file_storage;
    int worker_to_master_pipe = worker_args->worker_to_master_pipe_write_fd;
    unsigned int max_num_files = worker_args->max_num_files;
    unsigned int max_storage_size = worker_args->max_storage_size;

    rw_lock_t* storage_lock = get_rw_lock_from_storage(file_storage);

    for (;;) {
        void* client_fd_ptr;
        int get_res;
        DIE_NEG1(get_res = usbuf_get(master_to_workers_buf, &client_fd_ptr), "usbuf get");
        if (get_res == -2) {
            // the buffer is closed, so the worker should terminate
            return;
        }
        int client_fd = *(int*)client_fd_ptr;
        free(client_fd_ptr);

        // initialize the first client packet
        struct packet client_packet;
        clear_packet(&client_packet);

        // receive the client request
        int receive_res = receive_packet(client_fd, &client_packet);
        if (receive_res < 0) {
            // the client disconnected
            perror("receive packet");

            // cleanup the client fd in the entire structure before closing the
            // file descriptor. This prevents any possibility of any data race
            // caused by another client connecting with the same fd.
            client_cleanup(file_storage, client_fd);

            // finally close the client fd
            // (and do not send it back to the server)
            close(client_fd);

            // return to listening on the buffer
            continue;
        }

        switch (client_packet.op) {
        case OPEN_FILE:
            break;
        case READ_FILE:
            break;
        case READ_N_FILES:
            break;
        case WRITE_FILE:
            break;
        case APPEND_TO_FILE:
            break;
        case LOCK_FILE:
            break;
        case UNLOCK_FILE:
            break;
        case CLOSE_FILE:
            break;
        case REMOVE_FILE:
            break;
        default:
            break;
        }

        printf("worker %d got %d\n", client_fd, client_fd);
        writen(worker_to_master_pipe, &client_fd, sizeof(int));
    }
}

void* server_worker_entry_point(void* arg)
{
    // unpack all the arguments and cast them to the correct types
    thread_pool_arg_t* pool_arg = arg;
    worker_arg_t* worker_arg = pool_arg->common_arg;

    // call the actual worker
    server_worker(pool_arg->num_worker, worker_arg);
    return NULL;
}