#ifndef CONFIGPARSER_H
#define CONFIGPARSER_H

#include <stdio.h>

typedef struct config_s config_t;

config_t* get_config_from_file(const char* file_path);

void print_config(const config_t* config);

int config_get_next_entry(config_t* config, char** key, char** value);

void destroy_config(config_t* config);

#endif