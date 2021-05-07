#include <stdio.h>
#include <string.h>

#include "configparser.h"

int main(void)
{
    struct config_t* config;
    config = get_config_from_file("tests/example_config.txt");

    char *key, *value;
    int res;
    res = config_get_next_entry(config, &key, &value);
    if (res != 1 || strcmp(key, "A") != 0 || strcmp(value, "A") != 0)
        return -1;

    res = config_get_next_entry(config, &key, &value);
    if (res != 1 || strcmp(key, "B") != 0 || strcmp(value, "30") != 0)
        return -1;

    res = config_get_next_entry(config, &key, &value);
    if (res != 1 || strcmp(key, "C") != 0 || strcmp(value, "aaa") != 0)
        return -1;

    res = config_get_next_entry(config, &key, &value);
    if (res != 1 || strcmp(key, "vvv") != 0 || strcmp(value, "aaa") != 0)
        return -1;

    res = config_get_next_entry(config, &key, &value);
    if (res != 0)
        return -1;
    res = config_get_next_entry(config, &key, &value);
    if (res != 0)
        return -1;

    destroy_config(config);
    return 0;
}