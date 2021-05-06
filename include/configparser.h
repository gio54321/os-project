#ifndef CONFIGPARSER_H
#define CONFIGPARSER_H

#include <stdio.h>

struct config_entry {
    char* key;
    char* value;
    struct config_entry* next;
};

struct config_t {
    struct config_entry* start;
    struct config_entry* end;
};

struct config_t* get_config_from_file(const char* file_path);

void print_config(const struct config_t* config);

void destroy_config(struct config_t* config);

#endif