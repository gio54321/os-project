#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "configparser.h"
#include "file_storage_internal.h"
#include "logger.h"
#include "server_worker.h"
#include "thread_pool.h"
#include "unbounded_shared_buffer.h"
#include "utils.h"

#define CONFIG_FILENAME "config.txt"

struct server_config {
    long num_workers;
    long max_num_files;
    long max_storage_size;
    bool enable_compression;
    char* socketname;
};

/**
 * Parse the config file config_filename
 * Return 0 on success and the resulting values are put into res
 * Return -1 on parsing error or invalid key
 */
int parse_config(const char* config_filename, struct server_config* res)
{
    config_t* config;
    DIE_NULL(config = get_config_from_file(config_filename), "get_config_from_file");

    char *key, *value;
    while (config_get_next_entry(config, &key, &value)) {
        if (strcmp(key, "num_workers") == 0) {
            long n;
            if (string_to_long(value, &n) == -1) {
                fprintf(stderr, "error: unable to convert %s to a long\n", value);
                goto cleanup;
            }
            if (n <= 0) {
                fprintf(stderr, "error: %s must be a positive integer\n", key);
                goto cleanup;
            }
            res->num_workers = n;
        } else if (strcmp(key, "max_num_files") == 0) {
            long n;
            if (string_to_long(value, &n) == -1) {
                fprintf(stderr, "error: unable to convert %s to a long\n", value);
                goto cleanup;
            }
            if (n <= 0) {
                fprintf(stderr, "error: %s must be a positive integer\n", key);
                goto cleanup;
            }
            res->max_num_files = n;
        } else if (strcmp(key, "max_storage_size") == 0) {
            long n;
            if (string_to_long(value, &n) == -1) {
                fprintf(stderr, "error: unable to convert %s to a long\n", value);
                goto cleanup;
            }
            if (n <= 0) {
                fprintf(stderr, "error: %s must be a positive integer\n", key);
                goto cleanup;
            }
            res->max_storage_size = n;
        } else if (strcmp(key, "enable_compression") == 0) {
            long n;
            if (string_to_long(value, &n) == -1) {
                fprintf(stderr, "error: unable to convert %s to a long\n", value);
                goto cleanup;
            }
            if (n != 0 && n != 1) {
                fprintf(stderr, "error: %s must be either 0 or 1\n", key);
                goto cleanup;
            }
            res->enable_compression = n;
        } else if (strcmp(key, "socketname") == 0) {
            DIE_NULL(res->socketname = malloc((strlen(value) + 1) * sizeof(char)), "malloc");
            strcpy(res->socketname, value);
        }
    }
    destroy_config(config);
    return 0;
cleanup:
    destroy_config(config);
    errno = EINVAL;
    return -1;
}

int main(void)
{
    int sig_handler_to_master_pipe[2];
    int workers_to_master_pipe[2];
    usbuf_t* master_to_workers_buffer;
    usbuf_t* logger_buffer;

    // initalize the shared buffers
    DIE_NULL(master_to_workers_buffer = usbuf_create(FIFO_POLICY), "usbuf create");
    DIE_NULL(logger_buffer = usbuf_create(FIFO_POLICY), "usbuf create");

    // create the pipes
    DIE_NEG1(pipe(workers_to_master_pipe), "pipe");
    DIE_NEG1(pipe(sig_handler_to_master_pipe), "pipe");

    // create the logger thread
    pthread_t logger_tid;
    DIE_NEG1(pthread_create(&logger_tid, NULL, logger_entry_point, logger_buffer), "pthread create");
    LOG(logger_buffer, "Server startup");

    // parse the config file and then log the read values
    struct server_config cfg;
    DIE_NEG1(parse_config(CONFIG_FILENAME, &cfg), "parse_config");
    LOG(logger_buffer, "Server config: num_workers=%ld", cfg.num_workers);
    LOG(logger_buffer, "Server config: max_num_files=%ld", cfg.max_num_files);
    LOG(logger_buffer, "Server config: max_storage_size=%ld", cfg.max_storage_size);
    LOG(logger_buffer, "Server config: enable_compression=%d", cfg.enable_compression);
    LOG(logger_buffer, "Server config: socketname=%s", cfg.socketname);

    // set up the common argunet that will be passed to all the workers
    worker_arg_t* worker_arg = malloc(sizeof(worker_arg_t));
    worker_arg->master_to_workers_buffer = master_to_workers_buffer;
    worker_arg->logger_buffer = logger_buffer;
    worker_arg->max_num_files = cfg.max_num_files;
    worker_arg->max_storage_size = cfg.max_storage_size;
    worker_arg->enable_compression = cfg.enable_compression;

    // create the workers thread pool
    thread_pool_t* workers_pool;
    DIE_NULL(workers_pool = thread_pool_create(cfg.num_workers, server_worker_entry_point, worker_arg), "thread_pool_create");

    // close the master workers buffer and join the workers pool
    DIE_NEG1(usbuf_close(master_to_workers_buffer), "usbuf close");
    DIE_NEG1(thread_pool_join(workers_pool), "thread_pool_join");

    // close the logger buffer and join the logger thread
    DIE_NEG1(usbuf_close(logger_buffer), "usbuf close");
    DIE_NEG1(pthread_join(logger_tid, NULL), "thread_pool_join");

    free(worker_arg);
    free(cfg.socketname);

    DIE_NEG1(usbuf_free(master_to_workers_buffer), "usbuf_free");
    DIE_NEG1(usbuf_free(logger_buffer), "usbuf_free");

    return 0;
}