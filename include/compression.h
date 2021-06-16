#ifndef COMPRESSION_H
#define COMPRESSION_H

#include <stdlib.h>

/**
 * Compress the data using RLE algorithm
 * compressed data is allocated accordingly and needs to be freed by the caller
 * input data is not modified
 * return 0 on success, -1 on error and errno is set appropriately
 * if the compressed data size exceed the uncompressed size, then 1 is returned
 * and res_data and res_size should be ignored.
*/
int compress_data(void* data, size_t size, void** res_data, size_t* res_size);

/**
 * Decompress the data using RLE algorithm
 * decompressed data is allocated accordingly and needs to be freed by the caller
 * the caller must provide the uncompressed size, that is assumed to be correct
 * input data is not modified
 * return 0 on success, -1 on error and errno is set appropriately
*/
int decompress_data(void* data, size_t size, void** res_data, size_t decompressed_size);

#endif