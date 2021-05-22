#ifndef FILE_STORAGE_INTERNAL_H
#define FILE_STORAGE_INTERNAL_H

#include <pthread.h>
#include <stdbool.h>
#include <sys/select.h>
#include <unistd.h>

#include "int_queue.h"

enum file_replacement_policy {
    FIFO_REPLACEMENT,
    LRU_REPLACEMENT,
    LFU_REPLACEMENT
};

typedef struct vfile {
    // metadata
    char* filename;
    size_t size;
    struct vfile* next;
    struct vfile* prev;

    fd_set opened_by;
    int locked_by;
    int_queue_t* lock_queue;

    // actual data
    void* data;
} vfile_t;

typedef struct file_storage {
    struct vfile* first;
    struct vfile* last;
    enum file_replacement_policy replacement_policy;
    pthread_mutex_t mutex;
    unsigned int num_files;
    size_t total_size;
} file_storage_t;

/*
 * Creates an empty file storage with given replacement policy.
 * The storage shall be destroyed using destoy_file_storage
 * Returns NULL on error and errno is set appropriately
*/
file_storage_t* create_file_storage(enum file_replacement_policy replacement_policy);

/**
 * Destroy a file storage object. The function destroys all the files that are
 * contained in it, so it is not safe to call this function in a multithreaded context
 * Returns -1 on error and errno is set appropriately
*/
int destroy_file_storage(file_storage_t* storage);

/*
 * Creates an empty vfile
 * The vfile shall be destroyed using destroy_vfile
 * Returns NULL on error and errno is set appropriately
*/
vfile_t* create_vfile();

/**
 * Destroy a vfile object.
 * Returns -1 on error and errno is set appropriately
*/
int destroy_vfile(vfile_t* vfile);

/**
 * Lock the file storage data structure.
 * Returns -1 on error and errno is set appropriately.
*/
int lock_file_storage(file_storage_t* storage);

/**
 * Unlock the file storage data structure.
 * Returns -1 on error and errno is set appropriately.
*/
int unlock_file_storage(file_storage_t* storage);

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
int add_vfile_to_storage(file_storage_t* storage, vfile_t* vfile);

/**
 * Remove a file from the storage.
 * The file is simply removed from the storage and it is up to the caller to destroy it.
 * As for add_vfile_to_storage, the caller now takes 'ownership' of the file.
 * Returns -1 on error and errno is set appropriately.
*/
int remove_file_from_storage(file_storage_t* storage, vfile_t* vfile);

/**
 * Returns a pointer to a victim file, chosen using the policy of the storage
 * Returns NULL on error and errno is set appropriately.
*/
vfile_t* choose_victim_file(file_storage_t* storage);

/**
 * Return a pointer to the file in the storage with given filename. If the file
 * is not found then the function returns NULL and errno is set to ENOENT
 * Returns NULL on error and errno is set appropriately.
*/
vfile_t* get_file_from_name(file_storage_t* storage, size_t filename_len, const char* filename);

/**
 * Returns true if a file named filename exists in the storage, false otherwise.
*/
bool exists_file_in_storage(file_storage_t* storage, size_t filename_len, const char* filename);
#endif