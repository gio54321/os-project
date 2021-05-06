#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "configparser.h"

#define INITIAL_BUF_LEN 3

static void destroy_config_entries(struct config_entry* start);

// parse a single line of config
// return 0 on successful parsing
// if success, then the information about the key and the value
// are passed to the caller
// If the parsing was not successful then the function print to stderr
// informations about the error and returns -1
// If the line is empty (for an information standpoint) then the function
// returns true and empty_line is set to true, otherwise empty_line is set
// to false
static int parse_line(
    const char* line,
    size_t len,
    const unsigned int line_number,
    ssize_t* key_start,
    ssize_t* key_end,
    ssize_t* value_start,
    ssize_t* value_end,
    bool* empty_line)
{
    *key_start = -1;
    *key_end = -1;
    *value_start = -1;
    *value_end = -1;
    // run a simple state machine to detect the key and the value of the config
    // entry
    enum {
        INIT_WHITE,
        KEY,
        AFTER_KEY_WHITE,
        VALUE,
        DEAD
    } state;
    state = INIT_WHITE;

    for (size_t i = 0; i <= len; ++i) {
        switch (state) {
        case INIT_WHITE:
            if (!isspace(line[i]) && line[i] != '\0' && line[i] != '\n') {
                state = KEY;
                *key_start = i;
            }
            break;
        case KEY:
            if (line[i] == '=' || isspace(line[i])) {
                state = AFTER_KEY_WHITE;
                *key_end = i;
            }
            break;
        case AFTER_KEY_WHITE:
            if (line[i] != '=' && !isspace(line[i])) {
                state = VALUE;
                *value_start = i;
            }
            break;
        case VALUE:
            if (isspace(line[i]) || line[i] == '\0') {
                *value_end = i;
                state = DEAD;
            }
            break;
        case DEAD:
            break;
        }
    }

    *empty_line = false;
    if (state == INIT_WHITE) {
        *empty_line = true;
        return 0;
    }

    if (*key_start < 0 || *key_end < 0 || *value_start < 0 || *value_end < 0) {
        fprintf(stderr, "configparser: syntax error on line\n%d | %s",
            line_number, line);
        return -1;
    }
    return 0;
}

static int insert_config_entry(struct config_t* config, char* key, char* value)
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

struct config_t* get_config_from_file(const char* file_path)
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

    struct config_t config;
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
        ssize_t key_start, key_end, value_start, value_end;
        bool empty_line;
        const size_t len = strnlen(buf, buffer_size);

        int line_parse_res = parse_line(buf, len, line_number,
            &key_start, &key_end, &value_start, &value_end, &empty_line);
        if (line_parse_res == -1) {
            free(buf);
            destroy_config_entries(config.start);
            fclose(fp);
            return NULL;
        }
        if (empty_line)
            continue;

        // allocate the key and value strings.
        const size_t key_len = key_end - key_start;
        const size_t value_len = value_end - value_start;
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
        strncpy(key, buf + key_start, key_len);
        key[key_len] = '\0';
        strncpy(value, buf + value_start, value_len);
        value[value_len] = '\0';

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
    struct config_t* result = malloc(sizeof(struct config_t));
    if (result == NULL) {
        destroy_config_entries(config.start);
        return NULL;
    }
    result->start = config.start;
    result->end = config.end;
    return result;
}

// prints the key-value pairs contained in config
void print_config(const struct config_t* config)
{
    for (struct config_entry* p = config->start; p != NULL; p = p->next) {
        printf("%s -> %s\n", p->key, p->value);
    }
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

void destroy_config(struct config_t* config)
{
    destroy_config_entries(config->start);
    free(config);
}
