#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "file_storage_internal.h"
#include "logger.h"
#include "protocol.h"
#include "server_worker.h"
#include "thread_pool.h"
#include "unbounded_shared_buffer.h"
#include "utils.h"

static int client_cleanup(file_storage_t* file_storage, int client_fd)
{
    return 0;
}

static int send_error(int client_fd, char err_code)
{
    struct packet err_packet;
    clear_packet(&err_packet);

    err_packet.op = ERROR;
    err_packet.err_code = err_code;
    return send_packet(client_fd, &err_packet);
}

static void server_worker(unsigned int num_worker, worker_arg_t* worker_args)
{
    usbuf_t* master_to_workers_buf = worker_args->master_to_workers_buffer;
    usbuf_t* logger_buffer = worker_args->logger_buffer;
    file_storage_t* file_storage = worker_args->file_storage;
    int worker_to_master_pipe = worker_args->worker_to_master_pipe_write_fd;
    long max_num_files = worker_args->max_num_files;
    long max_storage_size = worker_args->max_storage_size;

    rw_lock_t* storage_lock = get_rw_lock_from_storage(file_storage);

    LOG(logger_buffer, "Worker #%d started", num_worker);

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
        int receive_res;
        DIE_NEG1(receive_res = receive_packet(client_fd, &client_packet), "receive_packet");
        if (receive_res == 0) {
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
            LOG(logger_buffer, "client %d requested to open %s", client_fd, client_packet.filename);
            write_lock(storage_lock);
            vfile_t* file_to_open = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_open == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    // if the flag O_CREATE is set then create the file, else send
                    // an error to the client
                    if (client_packet.flags & O_CREATE) {
                        LOG(logger_buffer, "creating file %s", client_packet.filename);
                        DIE_NULL(file_to_open = create_vfile(), "create vfile");
                        file_to_open->filename = client_packet.filename;
                        client_packet.filename = NULL;
                        DIE_NEG1(add_vfile_to_storage(file_storage, file_to_open), "add file to storage");
                    } else {
                        LOG(logger_buffer, "file open requested for %s but the file does not exist in the storage", client_packet.filename);
                        DIE_NEG1(send_error(client_fd, FILE_DOES_NOT_EXIST), "send error");
                    }
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            }

            // se the client fd in the opened by set
            FD_SET(client_fd, &file_to_open->opened_by);

            // if the flag O_LOCK is set then try to lock the file
            if (client_packet.flags & O_LOCK) {
                if (file_to_open->locked_by == -1) {
                    LOG(logger_buffer, "client %d locked the file %s", client_fd, client_packet.filename);
                    file_to_open->locked_by = client_fd;
                } else {
                    LOG(logger_buffer, "client %d requested to lock the file %s but was already locked, operation failed", client_fd, client_packet.filename);
                    // if the file is already locked then fail
                    DIE_NEG1(send_error(client_fd, FILE_ALREADY_LOCKED), "send error");

                    // clear the client in the opened by set (the opertion failed, so
                    // the file is not opened by the client)
                    FD_CLR(client_fd, &file_to_open->opened_by);
                }
            }

            write_unlock(storage_lock);
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