#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void send_comp(int client_fd)
{
    struct packet err_packet;
    clear_packet(&err_packet);

    err_packet.op = COMP;
    DIE_NEG(send_packet(client_fd, &err_packet), "send packet");
}

/**
 * Fail all the lock operations blocked on a lock_queue
*/
static void flush_lock_queue(fd_set* queue, int fd_max, usbuf_t* logger_buffer)
{
    for (int i = 0; i <= fd_max; ++i) {
        if (FD_ISSET(i, queue)) {
            LOG(logger_buffer, "client %d was waiting on the lock queue, the lock operation fails", i);
            DIE_NEG1(send_error(i, FILE_DOES_NOT_EXIST), "send error");
        }
    }
}

/**
 * Eject one victim file from the storage.
 * If client_fd is negative, then the file is deleted, otherwise is sent to client_fd
*/
static void eject_one_file(int client_fd, file_storage_t* storage, usbuf_t* logger_buffer, vfile_t* file_to_exclude)
{
    vfile_t* victim;
    DIE_NULL(victim = choose_victim_file(storage, file_to_exclude), "choose_victim_file");

    // fail any lock operation on the file
    flush_lock_queue(&victim->lock_queue, victim->lock_queue_max, logger_buffer);

    remove_file_from_storage(storage, victim);

    if (client_fd >= 0) {
        LOG(logger_buffer, "replacement: sending %s to client %d, the new total size of the storage is %ld with %d files",
            victim->filename, client_fd, storage->total_size, storage->num_files);

        // send the file to the client
        struct packet file_packet;
        file_packet.op = FILE_P;
        file_packet.name_length = strlen(victim->filename);
        file_packet.filename = victim->filename;
        file_packet.data_size = victim->size;
        file_packet.data = victim->data;

        DIE_NEG(send_packet(client_fd, &file_packet), "send_packet");
    } else {
        LOG(logger_buffer, "replacement: deleting %s, the new total size of the storage is %ld with %d files",
            victim->filename, storage->total_size, storage->num_files);
    }

    DIE_NEG1(destroy_vfile(victim), "destroy_vfile");
}

/**
 * Send to the client ejected files (possibly 0) until space_needed
 * bytes are available to use in the storage
*/
static void eject_files(int client_fd, long space_needed, long max_storage_size, file_storage_t* storage, usbuf_t* logger_buffer, vfile_t* file_to_exclude)
{
    while (storage->total_size + space_needed > max_storage_size) {
        eject_one_file(client_fd, storage, logger_buffer, file_to_exclude);
    }
}

static void unlock_file(vfile_t* file_to_unlock, usbuf_t* logger_buffer)
{
    if (file_to_unlock->lock_queue_max > 0) {
        // give the lock to another client
        FD_CLR(file_to_unlock->lock_queue_max, &file_to_unlock->lock_queue);
        int unlock_fd = file_to_unlock->lock_queue_max;

        LOG(logger_buffer, "the mutual exclusion of the file %s is given to %d", file_to_unlock->filename, unlock_fd);

        // re calculate the maximum of the set
        for (int i = file_to_unlock->lock_queue_max; i >= 0; --i) {
            if (FD_ISSET(i, &file_to_unlock->lock_queue)) {
                file_to_unlock->lock_queue_max = i;
            }
        }
        if (file_to_unlock->lock_queue_max == unlock_fd) {
            file_to_unlock->lock_queue_max = 0;
        }
        file_to_unlock->locked_by = unlock_fd;
        send_comp(unlock_fd);
    } else {
        LOG(logger_buffer, "the file %s is now locked by no client", file_to_unlock->filename);
        file_to_unlock->locked_by = -1;
    }
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
            LOG(logger_buffer, "Worker %d terminated", num_worker);
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
            LOG(logger_buffer, "client %d disconnected, cleaning up the storage", client_fd);

            // cleanup the client fd in the entire structure before closing the
            // file descriptor. This prevents any possibility of any data race
            // caused by another client connecting with the same fd.
            client_cleanup(file_storage, client_fd);

            // finally close the client fd
            close(client_fd);

            // send back -1 to the main thread so that we con notify that the client disconnected
            int neg1 = -1;
            DIE_NEG1(writen(worker_to_master_pipe, &neg1, sizeof(int)), "writen");

            // return to listening on the buffer
            continue;
        }

        switch (client_packet.op) {
        case OPEN_FILE:
            LOG(logger_buffer, "client %d requested to open %s", client_fd, client_packet.filename);
            write_lock(storage_lock);
            bool completed = false;
            vfile_t* file_to_open = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_open == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    // if the flag O_CREATE is set then create the file, else send
                    // an error to the client
                    if (client_packet.flags & O_CREATE) {
                        if (file_storage->num_files + 1 > max_num_files) {
                            // delete one file from the storage
                            eject_one_file(-1, file_storage, logger_buffer, NULL);
                        }
                        LOG(logger_buffer, "creating file %s", client_packet.filename);
                        DIE_NULL(file_to_open = create_vfile(), "create vfile");
                        file_to_open->filename = client_packet.filename;
                        client_packet.filename = NULL;
                        DIE_NEG1(add_vfile_to_storage(file_storage, file_to_open), "add file to storage");
                    } else {
                        LOG(logger_buffer, "file open requested for %s but the file does not exist in the storage", client_packet.filename);
                        DIE_NEG1(send_error(client_fd, FILE_DOES_NOT_EXIST), "send error");
                        completed = true;
                    }
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            }

            // if not already completed then handle the locking of the file
            if (!completed) {
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
                        completed = true;
                    }
                }
            }
            if (!completed) {
                send_comp(client_fd);
            }

            write_unlock(storage_lock);
            break;
        case READ_FILE:
            LOG(logger_buffer, "client %d requested to read %s", client_fd, client_packet.filename);
            read_lock(storage_lock);
            vfile_t* file_to_read = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_read == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    LOG(logger_buffer, "file read requested for %s but the file does not exist in the storage", client_packet.filename);
                    DIE_NEG1(send_error(client_fd, FILE_DOES_NOT_EXIST), "send error");
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            } else {
                if (file_to_read->locked_by != -1 && file_to_read->locked_by != client_fd) {
                    // the file is locked by another client
                    LOG(logger_buffer, "file read requested for %s but the file is already locked by client %d", client_packet.filename, file_to_read->locked_by);
                    DIE_NEG1(send_error(client_fd, FILE_IS_LOCKED_BY_ANOTHER_CLIENT), "send error");
                } else {
                    struct packet response;
                    clear_packet(&response);
                    response.op = DATA;
                    response.data_size = file_to_read->size;
                    response.data = file_to_read->data;
                    DIE_NEG(send_packet(client_fd, &response), "send packet");
                    LOG(logger_buffer, "%ld bytes sent to client %d for reading %s", file_to_read->size, client_fd, client_packet.filename);
                }
            }
            read_unlock(storage_lock);
            break;
        case READ_N_FILES:
            LOG(logger_buffer, "client %d requested to read %ld files", client_fd, client_packet.count);
            read_lock(storage_lock);
            // the client only allows the values of cout to be either a positive
            // integer or -1
            if (client_packet.count == -1) {
                // -1 means read all files
                client_packet.count = file_storage->num_files;
            }
            vfile_t* curr_file = file_storage->first;
            for (long i = 0; i < client_packet.count && curr_file != NULL; ++i) {
                struct packet file_packet;
                clear_packet(&file_packet);
                file_packet.op = FILE_P;
                file_packet.name_length = strlen(curr_file->filename);
                file_packet.filename = curr_file->filename;
                file_packet.data_size = curr_file->size;
                file_packet.data = curr_file->data;
                DIE_NEG(send_packet(client_fd, &file_packet), "send_packet");
                curr_file = curr_file->next;
            }
            send_comp(client_fd);
            read_unlock(storage_lock);
            break;
        case WRITE_FILE:
            LOG(logger_buffer, "client %d requested to write %s", client_fd, client_packet.filename);
            write_lock(storage_lock);
            vfile_t* file_to_write = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_write == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    LOG(logger_buffer, "file write requested for %s but the file does not exist in the storage", client_packet.filename);
                    DIE_NEG1(send_error(client_fd, FILE_DOES_NOT_EXIST), "send error");
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            } else {
                if (file_to_write->size != 0) {
                    // the file has been written already, so the write operation is invalid
                    LOG(logger_buffer, "file write requested for %s but the file has already been written to", client_packet.filename);
                    DIE_NEG1(send_error(client_fd, FILE_WAS_ALREADY_WRITTEN), "send error");
                } else {
                    if (file_to_write->locked_by != client_fd && file_to_write->locked_by != -1) {
                        // the file is locked by another client
                        LOG(logger_buffer, "file write requested for %s but the file is already locked by client %d", client_packet.filename, file_to_read->locked_by);
                        DIE_NEG1(send_error(client_fd, FILE_IS_LOCKED_BY_ANOTHER_CLIENT), "send error");
                    } else {
                        if (client_packet.data_size > max_storage_size) {
                            // the file is locked by another client
                            LOG(logger_buffer, "file write requested for %s but the file is too big (%ld bytes)", client_packet.filename, client_packet.data_size);
                            DIE_NEG1(send_error(client_fd, FILE_IS_TOO_BIG), "send error");
                        } else {
                            // eject files
                            eject_files(client_fd, client_packet.data_size, max_storage_size, file_storage, logger_buffer, NULL);

                            // write the data to the file
                            file_to_write->size = client_packet.data_size;
                            file_to_write->data = client_packet.data;
                            client_packet.data = NULL;

                            // increment the total storage size
                            file_storage->total_size += client_packet.data_size;

                            send_comp(client_fd);
                            LOG(logger_buffer, "%ld bytes written by client %d into file %s", client_packet.data_size, client_fd, client_packet.filename);
                        }
                    }
                }
            }

            write_unlock(storage_lock);
            break;
        case APPEND_TO_FILE:
            LOG(logger_buffer, "client %d requested to append to %s", client_fd, client_packet.filename);
            write_lock(storage_lock);
            vfile_t* file_to_append = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_append == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    LOG(logger_buffer, "file append requested for %s but the file does not exist in the storage", client_packet.filename);
                    DIE_NEG1(send_error(client_fd, FILE_DOES_NOT_EXIST), "send error");
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            } else {
                if (file_to_append->locked_by != client_fd && file_to_append->locked_by != -1) {
                    // the file is locked by another client
                    LOG(logger_buffer, "file append requested for %s but the file is already locked by client %d", client_packet.filename, file_to_read->locked_by);
                    DIE_NEG1(send_error(client_fd, FILE_IS_LOCKED_BY_ANOTHER_CLIENT), "send error");
                } else {
                    if (client_packet.data_size + file_to_append->size > max_storage_size) {
                        // the file is locked by another client
                        LOG(logger_buffer, "file append requested for %s but the data is too big (%ld bytes)", client_packet.filename, client_packet.data_size);
                        DIE_NEG1(send_error(client_fd, FILE_IS_TOO_BIG), "send error");
                    } else {
                        // eject files
                        eject_files(client_fd, client_packet.data_size, max_storage_size, file_storage, logger_buffer, file_to_append);

                        // append the data to the file
                        size_t offset = file_to_append->size;
                        file_to_append->size += client_packet.data_size;
                        DIE_NULL(file_to_append->data = realloc(file_to_append->data, file_to_append->size), "realloc");
                        memcpy(file_to_append->data + offset, client_packet.data, client_packet.data_size);

                        // increment the total storage size
                        file_storage->total_size += client_packet.data_size;

                        send_comp(client_fd);
                        LOG(logger_buffer, "%ld bytes appended by client %d into file %s", client_packet.data_size, client_fd, client_packet.filename);
                    }
                }
            }

            write_unlock(storage_lock);
            break;
        case LOCK_FILE:
            LOG(logger_buffer, "client %d requested to lock %s", client_fd, client_packet.filename);
            write_lock(storage_lock);
            vfile_t* file_to_lock = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_lock == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    LOG(logger_buffer, "file lock requested for %s but the file does not exist in the storage", client_packet.filename);
                    DIE_NEG1(send_error(client_fd, FILE_DOES_NOT_EXIST), "send error");
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            } else {
                if (file_to_lock->locked_by == client_fd) {
                    // the file has been written already, so the write operation is invalid
                    LOG(logger_buffer, "file lock requested for %s but the file was already locked by the client", client_packet.filename);
                    DIE_NEG1(send_error(client_fd, FILE_ALREADY_LOCKED), "send error");
                } else {
                    if (file_to_lock->locked_by == -1) {
                        file_to_lock->locked_by = client_fd;
                        send_comp(client_fd);
                    } else {
                        // put the client fd into the lock waiting queue
                        FD_SET(client_fd, &file_to_lock->lock_queue);
                        if (client_fd > file_to_lock->lock_queue_max) {
                            file_to_lock->lock_queue_max = client_fd;
                        }
                        // NB: do not send comp, the operation does not complete until
                        // the owner of the lock releases it
                    }
                }
            }
            write_unlock(storage_lock);
            break;
        case UNLOCK_FILE:
            LOG(logger_buffer, "client %d requested to unlock %s", client_fd, client_packet.filename);
            write_lock(storage_lock);
            vfile_t* file_to_unlock = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_unlock == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    LOG(logger_buffer, "file unlock requested for %s but the file does not exist in the storage", client_packet.filename);
                    DIE_NEG1(send_error(client_fd, FILE_DOES_NOT_EXIST), "send error");
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            } else {
                send_comp(client_fd);
                if (file_to_unlock->locked_by != client_fd) {
                    // the file has been written already, so the write operation is invalid
                    LOG(logger_buffer, "file unlock requested for %s but the file was locked by another client", client_packet.filename);
                    DIE_NEG1(send_error(client_fd, FILE_IS_NOT_LOCKED), "send error");
                } else {
                    unlock_file(file_to_unlock, logger_buffer);
                }
            }
            write_unlock(storage_lock);
            break;
        case CLOSE_FILE:
            LOG(logger_buffer, "client %d requested to close %s", client_fd, client_packet.filename);
            write_lock(storage_lock);
            vfile_t* file_to_close = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_close == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    LOG(logger_buffer, "file removal requested for %s but the file does not exist in the storage", client_packet.filename);
                    DIE_NEG1(send_error(client_fd, FILE_DOES_NOT_EXIST), "send error");
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            } else {
                // the file lockedBy can be ignored for the close operation
                // check if the file is actually opened by the client
                if (!FD_ISSET(client_fd, &file_to_close->opened_by)) {
                    // file is not opened by the client, send error
                    LOG(logger_buffer, "file removal requested for %s but the file is not opened by the client %d", client_packet.filename, file_to_close->locked_by);
                    DIE_NEG1(send_error(client_fd, FILE_IS_NOT_OPENED), "send error");
                } else {
                    // remove the client from the file's open set
                    // then send completion packet
                    FD_CLR(client_fd, &file_to_close->opened_by);

                    // if the client is the owner of the lock, then unlock the file
                    if (file_to_close->locked_by == client_fd) {
                        unlock_file(file_to_close, logger_buffer);
                    }

                    send_comp(client_fd);
                }
            }
            write_unlock(storage_lock);
            break;
        case REMOVE_FILE:
            LOG(logger_buffer, "client %d requested to remove %s", client_fd, client_packet.filename);
            write_lock(storage_lock);
            vfile_t* file_to_remove = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_remove == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    LOG(logger_buffer, "file removal requested for %s but the file does not exist in the storage", client_packet.filename);
                    DIE_NEG1(send_error(client_fd, FILE_DOES_NOT_EXIST), "send error");
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            } else {
                if (file_to_remove->locked_by != -1 && file_to_remove->locked_by != client_fd) {
                    // the file is locked by another client
                    LOG(logger_buffer, "file removal requested for %s but the file is already locked by client %d", client_packet.filename, file_to_remove->locked_by);
                    DIE_NEG1(send_error(client_fd, FILE_IS_LOCKED_BY_ANOTHER_CLIENT), "send error");
                } else {
                    if (!FD_ISSET(client_fd, &file_to_remove->opened_by)) {
                        // file is not opened by the client, send error
                        LOG(logger_buffer, "file removal requested for %s but the file is not opened by the client", client_packet.filename);
                        DIE_NEG1(send_error(client_fd, FILE_IS_NOT_OPENED), "send error");
                    } else {
                        // remove the file from the storage and then free the associated memoty
                        // then send completion packet
                        DIE_NEG1(remove_file_from_storage(file_storage, file_to_remove), "remove_file_from_storage");

                        // fail any pending locks for this file
                        flush_lock_queue(&file_to_remove->lock_queue, file_to_remove->lock_queue_max, logger_buffer);
                        destroy_vfile(file_to_remove);
                        send_comp(client_fd);
                    }
                }
            }
            write_unlock(storage_lock);
            break;
        default:
            break;
        }

        // destroy the received packet
        destroy_packet(&client_packet);
        // the request terminated, so return to the main thread the fd of the client
        DIE_NEG1(writen(worker_to_master_pipe, &client_fd, sizeof(int)), "writen");
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