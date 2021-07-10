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

static void send_error(int client_fd, char err_code)
{
    struct packet err_packet;
    clear_packet(&err_packet);

    err_packet.op = ERROR;
    err_packet.err_code = err_code;
    DIE_NEG_IGN_EPIPE(send_packet(client_fd, &err_packet), "send_error");
}

static void send_comp(int client_fd)
{
    struct packet err_packet;
    clear_packet(&err_packet);

    err_packet.op = COMP;
    DIE_NEG_IGN_EPIPE(send_packet(client_fd, &err_packet), "send packet");
}

/**
 * Fail all the lock operations blocked on a lock_queue
*/
static void flush_lock_queue(fd_set* queue, int fd_max, usbuf_t* logger_buffer, int num_worker, int client_fd, const char* op)
{
    for (int i = 0; i <= fd_max; ++i) {
        if (FD_ISSET(i, queue)) {
            LOG(logger_buffer, "[W:%02d] [C:%02d] [%s] INFO client %d was waiting in the lock queue, fail the lock operation", num_worker, client_fd, op, i);
            send_error(i, FILE_DOES_NOT_EXIST);
        }
    }
}

/**
 * Eject one victim file from the storage.
 * If client_fd is negative, then the file is deleted, otherwise is sent to client_fd
*/
static void eject_one_file(int client_fd, file_storage_t* storage, usbuf_t* logger_buffer, vfile_t* file_to_exclude, int num_worker, const char* op)
{
    vfile_t* victim;
    DIE_NULL(victim = choose_victim_file(storage, file_to_exclude), "choose_victim_file");

    // increment statistics for number of replacements
    ++storage->statistics.num_replacements;

    // fail any lock operation on the file
    flush_lock_queue(&victim->lock_queue, victim->lock_queue_max, logger_buffer, num_worker, client_fd, op);

    remove_file_from_storage(storage, victim);

    if (client_fd >= 0) {
        LOG(logger_buffer, "[W:%02d] [C:%02d] [%s] INFO REPLACEMENT {op:send, file:%s, new_size:%zd, num_files:%d}", num_worker, client_fd, op,
            victim->filename, storage->total_size, storage->num_files);

        // send the file to the client
        struct packet file_packet;
        file_packet.op = FILE_P;
        file_packet.name_length = strlen(victim->filename);
        file_packet.filename = victim->filename;
        file_packet.data_size = victim->size;
        file_packet.data = victim->data;

        DIE_NEG_IGN_EPIPE(send_packet(client_fd, &file_packet), "send_packet");
    } else {
        LOG(logger_buffer, "[W:%02d] [C:%02d] [%s] INFO REPLACEMENT {op:delete, file:%s, new_size:%zd, num_files:%d}", num_worker, client_fd, op,
            victim->filename, storage->total_size, storage->num_files);
    }

    DIE_NEG1(destroy_vfile(victim), "destroy_vfile");
}

/**
 * Send to the client ejected files (possibly 0) until space_needed
 * bytes are available to use in the storage
*/
static void eject_files(int client_fd, long space_needed, long max_storage_size,
    file_storage_t* storage, usbuf_t* logger_buffer, vfile_t* file_to_exclude, int num_worker, const char* op)
{
    while (storage->total_size + space_needed > max_storage_size) {
        eject_one_file(client_fd, storage, logger_buffer, file_to_exclude, num_worker, op);
    }
}

static void unlock_file(vfile_t* file_to_unlock, usbuf_t* logger_buffer, int num_worker, int client_fd, const char* op)
{
    if (file_to_unlock->lock_queue_max > 0) {
        // give the lock to another client
        FD_CLR(file_to_unlock->lock_queue_max, &file_to_unlock->lock_queue);
        int unlock_fd = file_to_unlock->lock_queue_max;

        LOG(logger_buffer, "[W:%02d] [C:%02d] [%s] INFO file is locked by %d", num_worker, client_fd, op, unlock_fd);

        // re calculate the maximum of the set
        for (int i = file_to_unlock->lock_queue_max; i >= 0; --i) {
            if (FD_ISSET(i, &file_to_unlock->lock_queue)) {
                file_to_unlock->lock_queue_max = i;
            }
        }
        // if unlock_fd was the only one in the set, then reset the maximum
        if (file_to_unlock->lock_queue_max == unlock_fd) {
            file_to_unlock->lock_queue_max = 0;
        }

        // set unlock_fd as the owner of the lock of the file
        file_to_unlock->locked_by = unlock_fd;

        // complete the lock operation that was suspended until now
        send_comp(unlock_fd);
    } else {
        LOG(logger_buffer, "[W:%02d] [C:%02d] [%s] INFO file is not anymore locked", num_worker, client_fd, op);
        file_to_unlock->locked_by = -1;
    }
}
/**
 * Clean up the storage when one client disconnected
 * this means unlocking all the files locked, all the files opened
 * and possibly removing it from a lock queue. After this call it is safe
 * to close the file descriptor, so that it can be reused for other clients
 */
static void client_cleanup(file_storage_t* file_storage, int client_fd, usbuf_t* logger_buffer, int num_worker)
{
    for (vfile_t* curr_file = file_storage->first; curr_file != NULL; curr_file = curr_file->next) {
        if (curr_file->locked_by == client_fd) {
            LOG(logger_buffer, "[W:%02d] [C:%02d] [cleanup] INFO unlocking the file %s", num_worker, client_fd, curr_file->filename);
            unlock_file(curr_file, logger_buffer, num_worker, client_fd, "cleanup");
        }
        if (FD_ISSET(client_fd, &curr_file->opened_by)) {
            LOG(logger_buffer, "[W:%02d] [C:%02d] [cleanup] INFO closing the file %s", num_worker, client_fd, curr_file->filename);
            FD_CLR(client_fd, &curr_file->opened_by);
        }
        if (FD_ISSET(client_fd, &curr_file->lock_queue)) {
            LOG(logger_buffer, "[W:%02d] [C:%02d] [cleanup] INFO removing the client from the lock queue of %s", num_worker, client_fd, curr_file->filename);
            FD_CLR(client_fd, &curr_file->lock_queue);
            bool changed = false;
            for (int i = curr_file->lock_queue_max; i >= 0; --i) {
                if (FD_ISSET(i, &curr_file->lock_queue)) {
                    curr_file->lock_queue_max = i;
                    changed = true;
                    break;
                }
            }
            if (!changed) {
                curr_file->lock_queue_max = 0;
            }
        }
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

    unsigned int num_served_requests = 0;

    rw_lock_t* storage_lock = get_rw_lock_from_storage(file_storage);

    LOG(logger_buffer, "Worker #%d started", num_worker);

    for (;;) {
        void* client_fd_ptr;
        int get_res;
        DIE_NEG1(get_res = usbuf_get(master_to_workers_buf, &client_fd_ptr), "usbuf get");
        if (get_res == -2) {
            LOG(logger_buffer, "Terminated worker %d requests served: %d", num_worker, num_served_requests);
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
        receive_res = receive_packet(client_fd, &client_packet);
        if (receive_res == -1 && errno != ECONNRESET) {
            perror("receive packet");
            exit(EXIT_FAILURE);
        } else if (receive_res == 0 || (receive_res == -1 && errno == ECONNRESET)) {
            // the client disconnected
            write_lock(storage_lock);

            LOG(logger_buffer, "[W:%02d] [C:%02d] [disconnect] INFO client disconnected, starting cleanup", num_worker, client_fd);

            // cleanup the client fd in the entire structure before closing the
            // file descriptor. This prevents any possibility of any data race
            // caused by another client connecting with the same fd.
            client_cleanup(file_storage, client_fd, logger_buffer, num_worker);

            LOG(logger_buffer, "[W:%02d] [C:%02d] [cleanup] SUCCESS", num_worker, client_fd);

            write_unlock(storage_lock);

            // send back -client_fd to the main thread so that we con notify that the client disconnected
            int neg1 = -client_fd;
            DIE_NEG1(writen(worker_to_master_pipe, &neg1, sizeof(int)), "writen");

            // return to listening on the buffer
            continue;
        }

        // increment number of requests served by the worker
        ++num_served_requests;

        switch (client_packet.op) {
        case OPEN_FILE:
            write_lock(storage_lock);
            LOG(logger_buffer, "[W:%02d] [C:%02d] [open] REQUEST {file:%s; lock:%d; create:%d}",
                num_worker, client_fd, client_packet.filename, (client_packet.flags & O_LOCK) > 0, (client_packet.flags & O_CREATE) > 0);
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
                            eject_one_file(-1, file_storage, logger_buffer, NULL, num_worker, "open");
                        }
                        DIE_NULL(file_to_open = create_vfile(), "create vfile");
                        file_to_open->filename = client_packet.filename;
                        client_packet.filename = NULL;
                        DIE_NEG1(add_vfile_to_storage(file_storage, file_to_open), "add file to storage");

                        // increment max of num_files if needed
                        if (file_storage->num_files > file_storage->statistics.maximum_num_files) {
                            file_storage->statistics.maximum_num_files = file_storage->num_files;
                        }
                    } else {
                        LOG(logger_buffer, "[W:%02d] [C:%02d] [open] ERROR FILE_DOES_NOT_EXIST", num_worker, client_fd);
                        send_error(client_fd, FILE_DOES_NOT_EXIST);
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
                        LOG(logger_buffer, "[W:%02d] [C:%02d] [open] INFO client locked the file", num_worker, client_fd);
                        file_to_open->locked_by = client_fd;
                    } else {
                        LOG(logger_buffer, "[W:%02d] [C:%02d] [open] ERROR FILE_ALREADY_LOCKED", num_worker, client_fd);
                        // if the file is already locked then fail
                        send_error(client_fd, FILE_ALREADY_LOCKED);

                        // clear the client in the opened by set (the opertion failed, so
                        // the file is not opened by the client)
                        FD_CLR(client_fd, &file_to_open->opened_by);
                        completed = true;
                    }
                }
            }
            if (!completed) {
                LOG(logger_buffer, "[W:%02d] [C:%02d] [open] SUCCESS", num_worker, client_fd);
                send_comp(client_fd);
            }

            write_unlock(storage_lock);
            break;
        case READ_FILE:
            read_lock(storage_lock);
            LOG(logger_buffer, "[W:%02d] [C:%02d] [read] REQUEST {file:%s}", num_worker, client_fd, client_packet.filename);
            vfile_t* file_to_read = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_read == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [read] ERROR FILE_DOES_NOT_EXIST", num_worker, client_fd);
                    send_error(client_fd, FILE_DOES_NOT_EXIST);
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            } else {
                if (file_to_read->locked_by != -1 && file_to_read->locked_by != client_fd) {
                    // the file is locked by another client
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [read] ERROR FILE_IS_LOCKED_BY_ANOTHER_CLIENT", num_worker, client_fd);
                    send_error(client_fd, FILE_IS_LOCKED_BY_ANOTHER_CLIENT);
                } else {
                    struct packet response;
                    clear_packet(&response);
                    response.op = DATA;
                    response.data_size = file_to_read->size;
                    response.data = file_to_read->data;
                    DIE_NEG_IGN_EPIPE(send_packet(client_fd, &response), "send packet");
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [read] SUCCESS {sent_bytes:%zd}", num_worker, client_fd, file_to_read->size);
                }
            }
            read_unlock(storage_lock);
            break;
        case READ_N_FILES:
            read_lock(storage_lock);
            LOG(logger_buffer, "[W:%02d] [C:%02d] [read_n] REQUEST {n:%ld}", num_worker, client_fd, client_packet.count);
            // the client only allows the values of cout to be either a positive
            // integer or -1
            if (client_packet.count <= 0) {
                // count <= 0  means read all files
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
                DIE_NEG_IGN_EPIPE(send_packet(client_fd, &file_packet), "send_packet");
                LOG(logger_buffer, "[W:%02d] [C:%02d] [read_n] INFO sent file {filename:%s; sent_bytes:%zd}",
                    num_worker, client_fd, curr_file->filename, curr_file->size);
                curr_file = curr_file->next;
            }
            send_comp(client_fd);
            LOG(logger_buffer, "[W:%02d] [C:%02d] [read_n] SUCCESS", num_worker, client_fd);
            read_unlock(storage_lock);
            break;
        case WRITE_FILE:
            write_lock(storage_lock);
            LOG(logger_buffer, "[W:%02d] [C:%02d] [write] REQUEST {file:%s}", num_worker, client_fd, client_packet.filename);
            vfile_t* file_to_write = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_write == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [write] ERROR FILE_DOES_NOT_EXIST", num_worker, client_fd);
                    send_error(client_fd, FILE_DOES_NOT_EXIST);
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            } else {
                if (file_to_write->size != 0) {
                    // the file has been written already, so the write operation is invalid
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [write] ERROR FILE_ALREADY_WRITTEN", num_worker, client_fd);
                    send_error(client_fd, FILE_WAS_ALREADY_WRITTEN);
                } else {
                    if (file_to_write->locked_by != client_fd && file_to_write->locked_by != -1) {
                        // the file is locked by another client
                        LOG(logger_buffer, "[W:%02d] [C:%02d] [write] ERROR FILE_IS_LOCKED_BY_ANOTHER_CLIENT", num_worker, client_fd);
                        send_error(client_fd, FILE_IS_LOCKED_BY_ANOTHER_CLIENT);
                    } else if (file_to_write->locked_by == -1) {
                        // the file is not locked by the client
                        LOG(logger_buffer, "[W:%02d] [C:%02d] [write] ERROR FILE_IS_NOT_LOCKED", num_worker, client_fd);
                        send_error(client_fd, FILE_IS_NOT_LOCKED);
                    } else {
                        if (client_packet.data_size > max_storage_size) {
                            // the file is locked by another client
                            LOG(logger_buffer, "[W:%02d] [C:%02d] [write] ERROR FILE_IS_TOO_BIG", num_worker, client_fd);
                            send_error(client_fd, FILE_IS_TOO_BIG);
                        } else {
                            // eject files
                            eject_files(client_fd, client_packet.data_size, max_storage_size, file_storage, logger_buffer, NULL, num_worker, "write");

                            // write the data to the file
                            file_to_write->size = client_packet.data_size;
                            file_to_write->data = client_packet.data;
                            client_packet.data = NULL;

                            // increment the total storage size
                            file_storage->total_size += client_packet.data_size;

                            send_comp(client_fd);
                            LOG(logger_buffer, "[W:%02d] [C:%02d] [write] SUCCESS {written_bytes:%zd}", num_worker, client_fd, client_packet.data_size);

                            // increment max of total size if needed
                            if (file_storage->total_size > file_storage->statistics.maximum_size_reached) {
                                file_storage->statistics.maximum_size_reached = file_storage->total_size;
                            }
                        }
                    }
                }
            }
            write_unlock(storage_lock);
            break;
        case APPEND_TO_FILE:
            write_lock(storage_lock);
            LOG(logger_buffer, "[W:%02d] [C:%02d] [append] REQUEST {file:%s}", num_worker, client_fd, client_packet.filename);
            vfile_t* file_to_append = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_append == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [append] ERROR FILE_DOES_NOT_EXIST", num_worker, client_fd);
                    send_error(client_fd, FILE_DOES_NOT_EXIST);
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            } else {
                if (file_to_append->locked_by != client_fd && file_to_append->locked_by != -1) {
                    // the file is locked by another client
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [append] ERROR FILE_IS_LOCKED_BY_ANOTHER_CLIENT", num_worker, client_fd);
                    send_error(client_fd, FILE_IS_LOCKED_BY_ANOTHER_CLIENT);
                } else {
                    if (client_packet.data_size + file_to_append->size > max_storage_size) {
                        // the file is locked by another client
                        LOG(logger_buffer, "[W:%02d] [C:%02d] [append] ERROR FILE_IS_TOO_BIG", num_worker, client_fd);
                        send_error(client_fd, FILE_IS_TOO_BIG);
                    } else {
                        // eject files
                        eject_files(client_fd, client_packet.data_size, max_storage_size, file_storage, logger_buffer, file_to_append, num_worker, "append");

                        // append the data to the file
                        size_t offset = file_to_append->size;
                        file_to_append->size += client_packet.data_size;
                        DIE_NULL(file_to_append->data = realloc(file_to_append->data, file_to_append->size), "realloc");
                        memcpy(file_to_append->data + offset, client_packet.data, client_packet.data_size);

                        // increment the total storage size
                        file_storage->total_size += client_packet.data_size;

                        send_comp(client_fd);
                        LOG(logger_buffer, "[W:%02d] [C:%02d] [append] SUCCESS {written_bytes:%zd}", num_worker, client_fd, client_packet.data_size);

                        // increment max of total size if needed
                        if (file_storage->total_size > file_storage->statistics.maximum_size_reached) {
                            file_storage->statistics.maximum_size_reached = file_storage->total_size;
                        }
                    }
                }
            }

            write_unlock(storage_lock);
            break;
        case LOCK_FILE:
            write_lock(storage_lock);
            LOG(logger_buffer, "[W:%02d] [C:%02d] [lock] REQUEST {file:%s}", num_worker, client_fd, client_packet.filename);
            vfile_t* file_to_lock = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_lock == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [lock] ERROR FILE_DOES_NOT_EXIST", num_worker, client_fd);
                    send_error(client_fd, FILE_DOES_NOT_EXIST);
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            } else {
                if (file_to_lock->locked_by == client_fd) {
                    // the file has been written already, so the write operation is invalid
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [lock] ERROR FILE_ALREADY_LOCKED", num_worker, client_fd);
                    send_error(client_fd, FILE_ALREADY_LOCKED);
                } else {
                    if (file_to_lock->locked_by == -1) {
                        file_to_lock->locked_by = client_fd;
                        send_comp(client_fd);
                        LOG(logger_buffer, "[W:%02d] [C:%02d] [lock] SUCCESS", num_worker, client_fd);
                    } else {
                        // put the client fd into the lock waiting queue
                        FD_SET(client_fd, &file_to_lock->lock_queue);
                        if (client_fd > file_to_lock->lock_queue_max) {
                            file_to_lock->lock_queue_max = client_fd;
                        }
                        LOG(logger_buffer, "[W:%02d] [C:%02d] [lock] INFO client is inserted into the witing queue", num_worker, client_fd);
                        // NB: do not send comp, the operation does not complete until
                        // the owner of the lock releases it
                    }
                }
            }
            write_unlock(storage_lock);
            break;
        case UNLOCK_FILE:
            write_lock(storage_lock);
            LOG(logger_buffer, "[W:%02d] [C:%02d] [unlock] REQUEST {file:%s}", num_worker, client_fd, client_packet.filename);
            fflush(stdout);
            vfile_t* file_to_unlock = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_unlock == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [unlock] ERROR FILE_DOES_NOT_EXIST", num_worker, client_fd);
                    send_error(client_fd, FILE_DOES_NOT_EXIST);
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            } else {
                if (file_to_unlock->locked_by != client_fd) {
                    // the file has been locked by another client,
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [unlock] ERROR FILE_IS_NOT_LOCKED", num_worker, client_fd);
                    send_error(client_fd, FILE_IS_NOT_LOCKED);
                } else {
                    send_comp(client_fd);
                    unlock_file(file_to_unlock, logger_buffer, num_worker, client_fd, "unlock");
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [unlock] SUCCESS", num_worker, client_fd);
                }
            }
            write_unlock(storage_lock);
            break;
        case CLOSE_FILE:
            write_lock(storage_lock);
            LOG(logger_buffer, "[W:%02d] [C:%02d] [close] REQUEST {file:%s}", num_worker, client_fd, client_packet.filename);
            vfile_t* file_to_close = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_close == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [close] ERROR FILE_DOES_NOT_EXIST", num_worker, client_fd);
                    send_error(client_fd, FILE_DOES_NOT_EXIST);
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            } else {
                // the file lockedBy can be ignored for the close operation
                // check if the file is actually opened by the client
                if (!FD_ISSET(client_fd, &file_to_close->opened_by)) {
                    // file is not opened by the client, send error
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [unlock] ERROR FILE_IS_NOT_OPENED", num_worker, client_fd);
                    send_error(client_fd, FILE_IS_NOT_OPENED);
                } else {
                    // remove the client from the file's open set
                    // then send completion packet
                    FD_CLR(client_fd, &file_to_close->opened_by);

                    // if the client is the owner of the lock, then unlock the file
                    if (file_to_close->locked_by == client_fd) {
                        unlock_file(file_to_close, logger_buffer, num_worker, client_fd, "close");
                    }

                    send_comp(client_fd);
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [close] SUCCESS", num_worker, client_fd);
                }
            }
            write_unlock(storage_lock);
            break;
        case REMOVE_FILE:
            write_lock(storage_lock);
            LOG(logger_buffer, "[W:%02d] [C:%02d] [remove] REQUEST {file:%s}", num_worker, client_fd, client_packet.filename);
            vfile_t* file_to_remove = get_file_from_name(file_storage, client_packet.name_length, client_packet.filename);
            if (file_to_remove == NULL) {
                if (errno = ENOENT) {
                    // file does not exists in the storage
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [remove] ERROR FILE_DOES_NOT_EXIST", num_worker, client_fd);
                    send_error(client_fd, FILE_DOES_NOT_EXIST);
                } else {
                    perror("get file from name");
                    exit(EXIT_FAILURE);
                }
            } else {
                if (file_to_remove->locked_by != -1 && file_to_remove->locked_by != client_fd) {
                    // the file is locked by another client
                    LOG(logger_buffer, "[W:%02d] [C:%02d] [remove] ERROR FILE_IS_LOCKED_BY_ANOTHER_CLIENT", num_worker, client_fd);
                    send_error(client_fd, FILE_IS_LOCKED_BY_ANOTHER_CLIENT);
                } else {
                    if (!FD_ISSET(client_fd, &file_to_remove->opened_by)) {
                        // file is not opened by the client, send error
                        LOG(logger_buffer, "[W:%02d] [C:%02d] [remove] ERROR FILE_IS_NOT_OPENED", num_worker, client_fd);
                        send_error(client_fd, FILE_IS_NOT_OPENED);
                    } else {
                        // remove the file from the storage and then free the associated memoty
                        // then send completion packet
                        DIE_NEG1(remove_file_from_storage(file_storage, file_to_remove), "remove_file_from_storage");

                        // fail any pending locks for this file
                        flush_lock_queue(&file_to_remove->lock_queue, file_to_remove->lock_queue_max, logger_buffer, num_worker, client_fd, "remove");
                        destroy_vfile(file_to_remove);
                        send_comp(client_fd);
                        LOG(logger_buffer, "[W:%02d] [C:%02d] [remove] SUCCESS", num_worker, client_fd);
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