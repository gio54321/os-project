#ifndef CONFIGPARSER_H
#define CONFIGPARSER_H

#include <stdio.h>

struct config_entry {
    char* key;
    char* value;
    struct config_entry* next;
};

typedef struct config_s {
    struct config_entry* start;
    struct config_entry* end;
    struct config_entry* curr;
} config_t;

config_t* get_config_from_file(const char* file_path);

void print_config(const config_t* config);

int config_get_next_entry(config_t* config, char** key, char** value);

void destroy_config(config_t* config);

#endif