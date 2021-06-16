#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "compression.h"

void run_test_success(unsigned char* input, size_t size, unsigned char* expected_output, size_t expected_size)
{
    void* res_buf;
    size_t res_size;

    assert(compress_data(input, size, &res_buf, &res_size) == 0);

    assert(res_size == expected_size);
    unsigned char* res_buf_c = (unsigned char*)res_buf;
    for (int i = 0; i < res_size; i++) {
        // printf("%d, exp %d\n", res_buf_c[i], expected_output[i]);
        assert(res_buf_c[i] == expected_output[i]);
    }

    void* uncompressed_buf;
    assert(decompress_data(res_buf, res_size, &uncompressed_buf, size) == 0);

    unsigned char* uncompressed_buf_c = (unsigned char*)uncompressed_buf;
    for (int i = 0; i < size; i++) {
        // printf("%d, exp %d\n", uncompressed_buf_c[i], input[i]);
        assert(uncompressed_buf_c[i] == input[i]);
    }
    free(uncompressed_buf);
    free(res_buf);
}
void run_test_fail(unsigned char* input, size_t size)
{
    void* res_buf;
    size_t res_size;
    assert(compress_data(input, size, &res_buf, &res_size) == 1);
}

int main(void)
{
    unsigned char case1[5] = { 5, 5, 5, 4, 4 };
    unsigned char case1_comp[4] = { 3, 5, 2, 4 };
    run_test_success(case1, 5, case1_comp, 4);

    unsigned char case2[4] = { 5, 5, 5, 4 };
    unsigned char case2_comp[4] = { 3, 5, 1, 4 };
    run_test_success(case2, 4, case2_comp, 4);

    unsigned char case3[5] = { 5, 5, 5, 4, 3 };
    run_test_fail(case3, 5);

    unsigned char case4[1000];
    for (size_t i = 0; i < 1000; ++i) {
        case4[i] = 42;
    }
    unsigned char case4_comp[8] = { 255, 42, 255, 42, 255, 42, 235, 42 };
    run_test_success(case4, 1000, case4_comp, 8);
    return 0;
}