#include <stdlib.h>

#include "compression.h"

/**
 * Compress the data using RLE algorithm
 * compressed data is allocated accordingly and needs to be freed by the caller
 * input data is not modified
 * return 0 on success, -1 on error and errno is set appropriately
 * if the compressed data size exceed the uncompressed size, then 1 is returned
 * and res_data and res_size should be ignored.
*/
int compress_data(void* data, size_t size, void** res_data, size_t* res_size)
{
    size_t buf_size = size;
    unsigned char* buf = malloc(sizeof(char) * buf_size);
    if (buf == NULL) {
        return -1;
    }

    // we assume that data points to a buffer
    // so we can interpret it as an array of chars
    unsigned char* c_data = (unsigned char*)data;

    size_t output_i = 0;
    for (size_t i = 0; i < size;) {
        char curr_byte = c_data[i];
        size_t curr_occ = 1;

        // count how many occurrences there are of the current but
        for (size_t j = i + 1; j < size && curr_occ < 255; ++j) {
            if (c_data[j] == curr_byte) {
                ++curr_occ;
            } else {
                break;
            }
        }

        if (output_i + 2 <= size) {
            // write on the output buffer the pair <occurrences, byte>
            buf[output_i] = curr_occ;
            buf[output_i + 1] = curr_byte;
        } else {
            // if the compressed data exceed the size of the input, then free
            // the temporary buffer and return -1
            free(buf);
            return 1;
        }
        output_i += 2;
        i += curr_occ;
    }

    // realloc the output buffer to match the actual size
    if (output_i < size) {
        buf = realloc(buf, sizeof(unsigned char) * output_i);
    }
    *res_data = buf;
    *res_size = output_i;
    return 0;
}

/**
 * Decompress the data using RLE algorithm
 * decompressed data is allocated accordingly and needs to be freed by the caller
 * the caller must provide the uncompressed size, that is assumed to be correct
 * input data is not modified
 * return 0 on success, -1 on error and errno is set appropriately
*/
int decompress_data(void* data, size_t size, void** res_data, size_t decompressed_size)
{
    unsigned char* buf = malloc(sizeof(char) * decompressed_size);
    if (buf == NULL) {
        return -1;
    }

    // we assume that data points to a buffer
    // so we can interpret it as an array of chars
    unsigned char* c_data = (unsigned char*)data;

    size_t output_i = 0;
    for (size_t i = 0; i < size; i += 2) {
        size_t curr_occ = c_data[i];
        char curr_byte = c_data[i + 1];
        for (size_t j = 0; j < curr_occ; ++j) {
            buf[output_i++] = curr_byte;
        }
    }

    *res_data = buf;
    return 0;
}