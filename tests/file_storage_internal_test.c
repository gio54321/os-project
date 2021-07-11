#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "file_storage_internal.h"

int main(void)
{
    file_storage_t* storage = create_file_storage(FIFO_REPLACEMENT);
    assert(storage != NULL);
    assert(storage->replacement_policy == FIFO_REPLACEMENT);

    vfile_t* f1 = create_vfile();
    vfile_t* f2 = create_vfile();
    vfile_t* f3 = create_vfile();
    assert(f1 != NULL && f2 != NULL && f3 != NULL);

    f1->filename = malloc(6 * sizeof(char));
    strcpy(f1->filename, "AAAAA");

    f2->filename = malloc(6 * sizeof(char));
    strcpy(f2->filename, "BBBBB");

    f3->filename = malloc(6 * sizeof(char));
    strcpy(f3->filename, "CCCCC");

    assert(add_vfile_to_storage(storage, f1) == 0);
    assert(storage->first == f1);
    assert(storage->last == f1);

    assert(get_file_from_name(storage, 5, "AAAAA") == f1);

    assert(add_vfile_to_storage(storage, f2) == 0);
    assert(storage->first == f1);
    assert(storage->last == f2);

    assert(get_file_from_name(storage, 5, "AAAAA") == f1);
    assert(get_file_from_name(storage, 5, "BBBBB") == f2);

    assert(add_vfile_to_storage(storage, f3) == 0);
    assert(storage->first == f1);
    assert(storage->last == f3);

    assert(get_file_from_name(storage, 5, "AAAAA") == f1);
    assert(get_file_from_name(storage, 5, "BBBBB") == f2);
    assert(get_file_from_name(storage, 5, "CCCCC") == f3);

    assert(remove_file_from_storage(storage, f2) == 0);
    assert(storage->first == f1);
    assert(storage->last == f3);

    assert(get_file_from_name(storage, 5, "AAAAA") == f1);
    assert(get_file_from_name(storage, 5, "BBBBB") == NULL && errno == ENOENT);
    assert(get_file_from_name(storage, 5, "CCCCC") == f3);

    assert(remove_file_from_storage(storage, f1) == 0);
    assert(storage->first == f3);
    assert(storage->last == f3);

    assert(get_file_from_name(storage, 5, "AAAAA") == NULL && errno == ENOENT);
    assert(get_file_from_name(storage, 5, "BBBBB") == NULL && errno == ENOENT);
    assert(get_file_from_name(storage, 5, "CCCCC") == f3);

    assert(remove_file_from_storage(storage, f3) == 0);
    assert(storage->first == NULL);
    assert(storage->last == NULL);

    assert(get_file_from_name(storage, 5, "AAAAA") == NULL && errno == ENOENT);
    assert(get_file_from_name(storage, 5, "BBBBB") == NULL && errno == ENOENT);
    assert(get_file_from_name(storage, 5, "CCCCC") == NULL && errno == ENOENT);

    assert(destroy_vfile(f1) == 0);
    assert(destroy_vfile(f2) == 0);
    assert(destroy_vfile(f3) == 0);
    assert(destroy_file_storage(storage) == 0);

    return 0;
}