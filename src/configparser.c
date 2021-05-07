#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "configparser.h"

#define INITIAL_BUF_LEN 3

static void destroy_config_entries(struct config_entry* start);

static int insert_config_entry(config_t* config, char* key, char* value)
{
    if (config->end == NULL) {
        struct config_entry* new_entry = malloc(sizeof(struct config_entry));
        if (new_entry == NULL) {
            return -1;
        }
        new_entry->key = key;
        new_entry->value = value;
        new_entry->next = NULL;

        config->end = new_entry;
        config->start = new_entry;
    } else {
        struct config_entry* new_entry = malloc(sizeof(struct config_entry));
        if (new_entry == NULL) {
            return -1;
        }
        new_entry->key = key;
        new_entry->value = value;
        new_entry->next = NULL;

        config->end->next = new_entry;
        config->end = new_entry;
    }
    return 0;
}

// parse a single line of config
// return 1 on successful parsing
// if success, then the substrings of the key and the value
// are passed to the caller
// If the parsing was not successful then the function print to stderr
// informations about the error and returns -1
// If the line is empty (only whitespaces) then the function returns 0
// key and value are pointers inside the region [line, line+len]
// so no other memory is allocated/freed
// line is modified, key and value are null terminated
static int parse_line(char* line, size_t len,
    const unsigned int line_number, char** key, char** value)
{
    // check if the line contains only white spaces i.e. spaces newline etc.
    // if it is the case, then return 0
    bool contains_only_whitespaces = true;
    for (size_t i = 0; i < len; ++i) {
        if (isspace(line[i]) == 0) {
            contains_only_whitespaces = false;
            break;
        }
    }
    if (contains_only_whitespaces) {
        return 0;
    }

    // create a backup line to output a meaningful error message,
    // since the line will be modified by the strtok_r call below
    char* line_backup = malloc(len * sizeof(char));
    if (line_backup == NULL)
        return -1;
    strncpy(line_backup, line, len);

    // parse three tokens with delimiters space, tab, newline and =
    // this means that a line like "A B" is valid with key A and value B,
    // some more precise parsing would be possible but
    // for the sake of semplicity it is not implemented
    char* strtok_state = NULL;

    const char* delimiters = "\t\n =";
    char* token1 = strtok_r(line, delimiters, &strtok_state);
    if (token1 == NULL) {
        fprintf(stderr, "configparser: syntax error on line\n%d | %s",
            line_number, line_backup);
        free(line_backup);
        return -1;
    }

    char* token2 = strtok_r(NULL, delimiters, &strtok_state);
    if (token2 == NULL) {
        fprintf(stderr, "configparser: syntax error on line\n%d | %s",
            line_number, line_backup);
        free(line_backup);
        return -1;
    }
    char* token3 = strtok_r(NULL, delimiters, &strtok_state);

    if (token3 != NULL) {
        fprintf(stderr, "configparser: syntax error on line\n%d | %s",
            line_number, line_backup);
        free(line_backup);
        return -1;
    }

    // set the results to point to the first two tokens found and then return
    *key = token1;
    *value = token2;

    free(line_backup);

    return 1;
}

config_t* get_config_from_file(const char* file_path)
{
    // try to open the file specified by file_path,
    // if it fails print the reason to stderr and return NULL
    FILE* fp = fopen(file_path, "r");
    if (fp == NULL) {
        perror("configparser");
        return NULL;
    }

    // allocate the inital buffer with size INITIAL_BUF_LEN, if the allocation
    // fails close the file and return NULL
    size_t buffer_size = INITIAL_BUF_LEN;

    char* buf = malloc(buffer_size * sizeof(char));
    if (buf == NULL) {
        fprintf(stderr, "configparser: error allocating memory\n");
        fclose(fp);
        return NULL;
    }

    config_t config;
    config.end = NULL;
    config.start = NULL;

    // Read the file line by line.
    // The reasonable assuption that there is
    // a maximum length possible for every line is not taken here, so
    // a more general algorithm that works for
    // arbitrary large line lengths is implemented.
    // Choosing a sensible INITIAL_BUF_LEN should avoid any realloc
    // in most practical use cases.
    // Every time the buffer needs to be reallocated, the size of
    // the buffer doubles, to get an asymptotically
    // logarithmic number of reallocs in respect of the line size
    bool eof_reached = false;
    unsigned int line_number = 1;

    while (!eof_reached) {
        // read at most buffer_size characters from file. If EOF then
        // fgets returns NULL, so we set eof_reached to true
        char* fgets_res = fgets(buf, buffer_size, fp);
        if (fgets_res == NULL) {
            eof_reached = true;
        }

        // realloc the buffer until the read string contsins a newline (so we
        // have successfully read the entire line) or we get EOF
        while (!eof_reached && strchr(buf, '\n') == 0) {
            // realloc the buffer twice its original size. If the realloc fails
            // print an error and return NULL
            buf = realloc(buf, buffer_size * 2);
            if (buf == NULL) {
                fprintf(stderr, "configparser: error allocating memory\n");
                fclose(fp);
                destroy_config_entries(config.start);
                return NULL;
            }

            // try to get the rest of the line. Here we use the same buffer
            // with an offset so that fgets writes in the newly allocated
            // portion of the buffer
            char* fgets_res = fgets(buf + buffer_size - 1, buffer_size + 1, fp);
            if (fgets_res == NULL) {
                eof_reached = true;
            }

            // finally double the buffer_size
            buffer_size *= 2;
        }

        // parse the line. If the line is empty then skip it
        // if the parsing failed, then return NULL
        const size_t len = strnlen(buf, buffer_size);

        char* key_ptr;
        char* value_ptr;
        int line_parse_res = parse_line(buf, len, line_number, &key_ptr, &value_ptr);
        if (line_parse_res == -1) {
            free(buf);
            destroy_config_entries(config.start);
            fclose(fp);
            return NULL;
        } else if (line_parse_res == 0) {
            // skip empty lines
            continue;
        }

        // allocate the key and value strings.
        // parse_line guarantees that key_ptr and value_ptr are null terminated
        // so we can safely use strlen
        const size_t key_len = strlen(key_ptr);
        const size_t value_len = strlen(value_ptr);
        char* key = malloc(sizeof(char) * (key_len + 1));
        char* value = malloc(sizeof(char) * (value_len + 1));
        if (key == NULL || value == NULL) {
            free(buf);
            fclose(fp);
            destroy_config_entries(config.start);
            if (key != NULL)
                free(key);
            if (value != NULL)
                free(value);
            return NULL;
        }

        // copy the key and the value strings in their final buffer
        strncpy(key, key_ptr, key_len + 1);
        strncpy(value, value_ptr, value_len + 1);

        // insert the config pair in the linked list.
        int insert_res = insert_config_entry(&config, key, value);
        if (insert_res == -1) {
            free(buf);
            fclose(fp);
            destroy_config_entries(config.start);
            free(key);
            free(value);
            return NULL;
        }

        ++line_number;
    }

    free(buf);
    fclose(fp);
    // allocate and initialize the final struct
    // that will be returned to the caller
    config_t* result = malloc(sizeof(config_t));
    if (result == NULL) {
        destroy_config_entries(config.start);
        return NULL;
    }
    result->start = config.start;
    result->end = config.end;
    result->curr = config.start;
    return result;
}

// prints the key-value pairs contained in config
void print_config(const config_t* config)
{
    for (struct config_entry* p = config->start; p != NULL; p = p->next) {
        printf("%s -> %s\n", p->key, p->value);
    }
}

int config_get_next_entry(config_t* config, char** key, char** value)
{
    if (config->curr == NULL) {
        return 0;
    }
    *key = config->curr->key;
    *value = config->curr->value;
    config->curr = config->curr->next;
    return 1;
}

static void destroy_config_entries(struct config_entry* start)
{
    for (struct config_entry* p = start; p != NULL;) {
        free(p->key);
        free(p->value);
        struct config_entry* old_p = p;
        p = p->next;
        free(old_p);
    }
}

void destroy_config(config_t* config)
{
    destroy_config_entries(config->start);
    free(config);
}
