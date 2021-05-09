#include <stdlib.h>

int open_connection(const char* sockname, int msec, const struct timespec abstime);
int close_connection(const char* sockname);

int open_file(const char* pathname, int flags);
int read_file(const char* pathname, void** buf, size_t* size);
int write_file(const char* pathname, const char* dirname);
int append_to_file(const char* pathname, void* buf, size_t size, const char* dirname);

int lock_file(const char* pathname);
int unlock_file(const char* pathname);

int close_file(const char* pathname);
int remove_file(const char* pathname);