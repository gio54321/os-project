#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "file_storage_internal.h"
#include "int_queue.h"

/*
 * Creates an empty file storage with given replacement policy.
 * The storage shall be destroyed using destoy_file_storage
 * Returns NULL on error and errno is set appropriately
*/
file_storage_t* create_file_storage(enum file_replacement_policy replacement_policy)
{
    file_storage_t* storage = malloc(sizeof(file_storage_t));
    if (storage == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    // initialize all the fields
    storage->first = NULL;
    storage->last = NULL;
    storage->rw_lock = create_rw_lock();
    if (storage->rw_lock == NULL) {
        return NULL;
    }
    storage->num_files = 0;
    storage->total_size = 0;
    storage->replacement_policy = replacement_policy;

    //initialize the statistics values
    storage->statistics.maximum_num_files = 0;
    storage->statistics.maximum_size_reached = 0;
    storage->statistics.num_replacements = 0;

    return storage;
}

/**
 * Destroy a file storage object. The function destroys all the files that are
 * contained in it, so it is not safe to call this function in a multithreaded context
 * Returns -1 on error and errno is set appropriately
*/
int destroy_file_storage(file_storage_t* storage)
{
    if (storage == NULL) {
        errno = EINVAL;
        return -1;
    }

    // destroy all the vfiles contained in the storage
    for (vfile_t* f = storage->first; f != NULL;) {
        vfile_t* tmp = f;
        f = f->next;
        destroy_vfile(tmp);
    }

    // destroy the mutex
    int destroy_res = destroy_rw_lock(storage->rw_lock);
    if (destroy_res == -1) {
        return -1;
    }
    free(storage);
    return 0;
}

/*
 * Creates an empty vfile
 * The vfile shall be destroyed using destroy_vfile
 * Returns NULL on error and errno is set appropriately
*/
vfile_t* create_vfile()
{
    vfile_t* vfile = malloc(sizeof(vfile_t));
    if (vfile == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    // initialize all the fields
    vfile->filename = NULL;
    vfile->size = 0;
    vfile->next = NULL;
    vfile->prev = NULL;
    FD_ZERO(&vfile->opened_by);
    vfile->locked_by = -1;
    FD_ZERO(&vfile->lock_queue);
    vfile->lock_queue_max = 0;
    vfile->data = NULL;
    vfile->used_counter = 0;
    vfile->last_used = time(NULL);

    if (pthread_mutex_init(&vfile->replacement_mutex, NULL) == -1) {
        return NULL;
    }

    return vfile;
}

/**
 * Destroy a vfile object.
 * Returns -1 on error and errno is set appropriately
*/
int destroy_vfile(vfile_t* vfile)
{
    if (vfile == NULL) {
        errno = EINVAL;
        return -1;
    }
    free(vfile->data);
    free(vfile->filename);
    if (pthread_mutex_destroy(&vfile->replacement_mutex) == -1) {
        return -1;
    }
    free(vfile);
    return 0;
}

/**
 * Get the rw lock contained in the storage
 * Each read operation to the storage must be done between read_lock() and read_unlock()
 * Each write operation to the storage must be done between write_lock() and write_unlock()
 * return NULL on error and errno is set apporopriately
*/
rw_lock_t* get_rw_lock_from_storage(file_storage_t* storage)
{
    if (storage == NULL) {
        errno = EINVAL;
        return NULL;
    }
    return storage->rw_lock;
}

/**
 * Add a vfile to a file storage.
 * It is up to the caller to ensure that a file with the same filename does not
 * already exists in the storage.
 * Also it is up to the caller to ensure that the file pointed by vfile is not
 * already present in the storage.
 * The file is not copyed, but instead is simply inserted inside the storage.
 * In a way, the storage takes 'ownership' of the file, so iits fields next and prev
 * shall not be modified by the user.
 * Returns -1 on error and errno is set appropriately.
*/
int add_vfile_to_storage(file_storage_t* storage, vfile_t* vfile)
{
    if (storage == NULL || vfile == NULL) {
        errno = EINVAL;
        return -1;
    }

    // update storage metadata
    storage->num_files++;
    storage->total_size += vfile->size;

    // initialize the next and prev fields (should not be necessary but can be
    // useful in the event that the file has been removed from a storage and next
    // and prev fields contain garbage)
    vfile->next = NULL;
    vfile->prev = NULL;

    // add vfile at the end of the double linked list
    if (storage->first == NULL) {
        storage->first = vfile;
    } else {
        storage->last->next = vfile;
        vfile->prev = storage->last;
    }
    storage->last = vfile;

    return 0;
}

/**
 * Remove a file from the storage.
 * The file is simply removed from the storage and it is up to the caller to destroy it.
 * As for add_vfile_to_storage, the caller now takes 'ownership' of the file.
 * Returns -1 on error and errno is set appropriately.
*/
int remove_file_from_storage(file_storage_t* storage, vfile_t* vfile)
{
    if (storage == NULL || vfile == NULL) {
        errno = EINVAL;
        return -1;
    }

    // update storage metadata
    storage->num_files--;
    storage->total_size -= vfile->size;

    // since the list is duobly linked, the remove operation is trivial
    // and can be done in constant time
    if (vfile->prev == NULL) {
        storage->first = vfile->next;
    } else {
        vfile->prev->next = vfile->next;
    }
    if (vfile->next == NULL) {
        storage->last = vfile->prev;
    } else {
        vfile->next->prev = vfile->prev;
    }
    return 0;
}

/**
 * Returns a pointer to a victim file, chosen using the policy of the storage
 * Returns NULL on error and errno is set appropriately.
*/
vfile_t* choose_victim_file(file_storage_t* storage, vfile_t* file_to_exclude)
{
    if (storage == NULL) {
        errno = EINVAL;
        return NULL;
    }

    vfile_t* min_file;
    switch (storage->replacement_policy) {
    case FIFO_REPLACEMENT:
        // for the FIFO policy it is trivial to choose the victim file: it is
        // just the first one in the list, if it is not the file_to_exclude, then
        // the second is returned
        if (file_to_exclude != NULL && storage->first == file_to_exclude) {
            return storage->first->next;
        } else {
            return storage->first;
        }
    case LFU_REPLACEMENT:
        // for the LFU policy we need to calculate the file that has the
        // minimum value for used_counter. If there are multiple files that have
        // an equal value of used_counter, then return one of them
        if (file_to_exclude != NULL && storage->first == file_to_exclude) {
            min_file = storage->first->next;
        } else {
            min_file = storage->first;
        }

        for (vfile_t* curr_file = min_file; curr_file != NULL; curr_file = curr_file->next) {
            if (curr_file != file_to_exclude && curr_file->used_counter < min_file->used_counter) {
                min_file = curr_file;
            }
        }

        return min_file;
    case LRU_REPLACEMENT:
        // for the LRU policy we need to calculate the file that has the
        // minimum value for last_used. If there are multiple files that have
        // an equal value of last_used, then return one of them
        if (file_to_exclude != NULL && storage->first == file_to_exclude) {
            min_file = storage->first->next;
        } else {
            min_file = storage->first;
        }

        for (vfile_t* curr_file = min_file; curr_file != NULL; curr_file = curr_file->next) {
            if (curr_file != file_to_exclude && curr_file->last_used < min_file->last_used) {
                min_file = curr_file;
            }
        }

        return min_file;
    default:
        fprintf(stderr, "error: replacmenent policy code is not valid\n");
        break;
    }
    return NULL;
}

/**
 * Return a pointer to the file in the storage with given filename. If the file
 * is not found then the function returns NULL and errno is set to ENOENT
 * Returns NULL on error and errno is set appropriately.
*/
vfile_t* get_file_from_name(file_storage_t* storage, size_t filename_len, const char* filename)
{
    if (storage == NULL || filename == NULL) {
        errno = EINVAL;
        return NULL;
    }

    // loop through all the files and return the matching one
    for (vfile_t* f = storage->first; f != NULL; f = f->next) {
        if (strncmp(filename, f->filename, filename_len) == 0) {
            return f;
        }
    }

    // if there has been no matches, return ENOENT error
    errno = ENOENT;
    return NULL;
}

/**
 * Atomically increment the used counter in vfile
 * This is safe to use even when the mutual exclusion of the entire storage
 * is acquired in read mode, because it uses an internal lock 
*/
int atomic_increment_used_counter(vfile_t* vfile)
{
    if (vfile == NULL) {
        errno = EINVAL;
        return -1;
    }

    int lock_res = pthread_mutex_lock(&vfile->replacement_mutex);
    if (lock_res == -1) {
        return -1;
    }

    ++vfile->used_counter;
    vfile->last_used = time(NULL);

    int unlock_res = pthread_mutex_unlock(&vfile->replacement_mutex);
    if (unlock_res == -1) {
        return -1;
    }

    return 0;
}