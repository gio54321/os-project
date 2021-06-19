#include <stdio.h>

#include "file_storage_api.h"
#include "protocol.h"

int main(int argc, char* argv[])
{
    struct timespec t;
    openConnection("./LSOfilestorage.sk", 100, t);
    openFile("config.txt", O_CREATE);
    writeFile("config.txt", "docs");
    openFile("A", O_CREATE);
    openFile("B", O_CREATE);
    lockFile("A");
    sleep(10);

    closeConnection("./LSOfilestorage.sk");

    return 0;
}