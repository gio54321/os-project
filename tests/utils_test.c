#include <assert.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

int main(void)
{
    int fds[2];
    pipe(fds);
    char dummy_data[10] = { 0, 1, 0, 12, 126, 30, 46, 50, 0, 53 };
    char dummy_string[6] = "AAAAA";
    char buf[10];

    // very basic test
    assert(writen(fds[1], dummy_data, 10) == 10);
    assert(readn(fds[0], buf, 10) == 10);

    for (size_t i = 0; i < 10; ++i) {
        assert(buf[i] == dummy_data[i]);
    }

    assert(writen(fds[1], dummy_string, 6) == 6);
    assert(readn(fds[0], buf, 6) == 6);

    assert(strcmp(dummy_string, buf) == 0);
    return 0;
}