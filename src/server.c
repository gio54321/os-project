#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "configparser.h"
#include "file_storage_internal.h"
#include "logger.h"
#include "server_worker.h"
#include "thread_pool.h"
#include "unbounded_shared_buffer.h"
#include "utils.h"

#define CONFIG_FILENAME "config.txt"

static int max(int a, int b)
{
    return (a > b) ? a : b;
}

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
    file_storage_t* file_storage;

    // initalize the shared buffers
    DIE_NULL(master_to_workers_buffer = usbuf_create(FIFO_POLICY), "usbuf create");
    DIE_NULL(logger_buffer = usbuf_create(FIFO_POLICY), "usbuf create");

    // create the pipes
    DIE_NEG1(pipe(workers_to_master_pipe), "pipe");
    DIE_NEG1(pipe(sig_handler_to_master_pipe), "pipe");

    // create the file storage
    DIE_NULL(file_storage = create_file_storage(FIFO_POLICY), "create_file_storage");

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

    // set up the common argument that will be passed to all the workers
    worker_arg_t* worker_arg = malloc(sizeof(worker_arg_t));
    worker_arg->master_to_workers_buffer = master_to_workers_buffer;
    worker_arg->worker_to_master_pipe_write_fd = workers_to_master_pipe[1];
    worker_arg->logger_buffer = logger_buffer;
    worker_arg->max_num_files = cfg.max_num_files;
    worker_arg->max_storage_size = cfg.max_storage_size;
    worker_arg->enable_compression = cfg.enable_compression;
    worker_arg->file_storage = file_storage;

    // create the workers thread pool
    thread_pool_t* workers_pool;
    DIE_NULL(workers_pool = thread_pool_create(cfg.num_workers, server_worker_entry_point, worker_arg), "thread_pool_create");

    // set up the socket
    int socket_fd;
    DIE_NEG1(socket_fd = socket(AF_UNIX, SOCK_STREAM, 0), "socket");
    struct sockaddr_un serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sun_family = AF_UNIX;
    strncpy(serv_addr.sun_path, cfg.socketname, strlen(cfg.socketname) + 1);
    DIE_NEG1(bind(socket_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)), "bind");
    DIE_NEG1(listen(socket_fd, SOMAXCONN), "listen");

    // set up the file descriptors sets for the select call
    fd_set listen_set, tmp_set;
    FD_ZERO(&listen_set);
    FD_ZERO(&tmp_set);
    FD_SET(sig_handler_to_master_pipe[0], &listen_set);
    FD_SET(workers_to_master_pipe[0], &listen_set);
    FD_SET(socket_fd, &listen_set);

    int fd_max = max(max(sig_handler_to_master_pipe[0], workers_to_master_pipe[0]), socket_fd);

    printf("socket fd %d\n", socket_fd);
    // main loop
    bool termina = false;
    while (!termina) {
        tmp_set = listen_set;

        DIE_NEG1(select(fd_max + 1, &tmp_set, NULL, NULL, NULL), "select");
        printf("select\n");
        for (int fd = 0; fd <= fd_max; ++fd) {
            if (FD_ISSET(fd, &tmp_set)) {
                printf("set %d", fd);

                if (fd == socket_fd) {
                    // new client connection request
                    int client_fd;
                    DIE_NEG1(client_fd = accept(socket_fd, NULL, 0), "accept");
                    FD_SET(client_fd, &listen_set);
                    if (client_fd > fd_max) {
                        fd_max = client_fd;
                    }
                    printf("new client %d\n", client_fd);
                } else if (fd == sig_handler_to_master_pipe[0]) {
                    // TODO handle signal
                } else if (fd == workers_to_master_pipe[0]) {
                    // handle worker request to put back into the set the fd
                    int put_back_fd;
                    DIE_NEG1(readn(workers_to_master_pipe[0], &put_back_fd, sizeof(int)), "readn");

                    // put the fd in the listen set
                    FD_SET(put_back_fd, &listen_set);
                    // update fd_max
                    if (put_back_fd > fd_max) {
                        fd_max = put_back_fd;
                    }

                } else {
                    // handle client's new request
                    printf("client request %d\n", fd);
                    int* client_fd;
                    DIE_NULL(client_fd = malloc(sizeof(int)), "malloc");
                    *client_fd = fd;
                    DIE_NEG1(usbuf_put(master_to_workers_buffer, client_fd), "usbuf_put");

                    // clear the fd from the listen set
                    FD_CLR(fd, &listen_set);

                    // re calculate fd_max
                    for (int j = fd_max; j >= 0; --j) {
                        if (FD_ISSET(j, &listen_set)) {
                            fd_max = j;
                            break;
                        }
                    }
                }
            }
        }
    }

    // close the master workers buffer and join the workers pool
    DIE_NEG1(usbuf_close(master_to_workers_buffer), "usbuf close");
    DIE_NEG1(thread_pool_join(workers_pool), "thread_pool_join");

    // close the logger buffer and join the logger thread
    DIE_NEG1(usbuf_close(logger_buffer), "usbuf close");
    DIE_NEG1(pthread_join(logger_tid, NULL), "thread_pool_join");

    free(worker_arg);
    free(cfg.socketname);

    DIE_NEG1(destroy_file_storage(file_storage), "destroy_file_storage");

    DIE_NEG1(usbuf_free(master_to_workers_buffer), "usbuf_free");
    DIE_NEG1(usbuf_free(logger_buffer), "usbuf_free");

    return 0;
}