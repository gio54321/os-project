#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "file_storage_api.h"
#include "protocol.h"
#include "utils.h"

#define CONNECTION_RETRY_MAX_TIME 3
#define CONNECTION_RETRY_INTERVAL 500

// theese two variables points to a location on the argv vector
char* sockname = NULL;
char* expelled_dirname = NULL;
char* read_dirname = NULL;

long request_interval = 0;

// extern variable to enable prints in storage api
extern bool FILE_STORAGE_API_PRINTS_ENABLED;

#define VALIDATE_BINARY_ARG(opt)                           \
    if (i + 1 < argc && argv[i + 1][0] != '-') {           \
        ++i;                                               \
    } else {                                               \
        fprintf(stderr, "option " #opt " is not unary\n"); \
        return -1;                                         \
    }

/**
 * Call the API function
 * Ignore EBADE, but exit on any other error
*/
#define API_CALL(call, name)    \
    do {                        \
        errno = 0;              \
        if (call == -1) {       \
            if (errno != EBADE) \
                perror(name);   \
            exit(EXIT_FAILURE); \
        }                       \
    } while (0)

static void print_help(char* program_name)
{
    printf("Usage: %s [OPTION]\n", program_name);
    printf("  -h\t\t\tprint this help and exit\n");
    printf("  -p\t\t\tenable debug prints\n");
    printf("  -f filename\t\tspecifies the socket name to connect\n");
    printf("  -w dirname[,n=0]\tsend at most n files in dirname, opening recursively all the subdirectories;\n\t\t\tif n=0 or non specified then all the files are sent\n");
    printf("  -W f1[,f2]\t\tlist of files to write to the server\n");
    printf("  -r f1[,f2]\t\tlist of file to read from the server\n");
    printf("  -R [n=0]\t\tread n files from server;\n\t\t\tif n=0 or non specified then all the files are read\n");
    printf("  -D dirname\t\tspecifies the directory for storing the ejected files\n");
    printf("  -d dirname\t\tspecifies the directory for storing the read files\n");
    printf("  -t time\t\ttime in milliseconds to wait between each request\n");
    printf("  -l f1[,f2]\t\tlist of file to lock on the server\n");
    printf("  -u f1[,f2]\t\tlist of file to unlock on the server\n");
    printf("  -c f1[,f2]\t\tlist of file to remove from the server\n");
}

/*
 * Validate command line arguments.
 * If -h is present, print the help and return -1
 * If no arguments are passed, print the help and return -1
 * If the arguments are valid, then return 0
*/
static int validate_args(int argc, char* argv[])
{
    bool writes_op = false;
    bool reads_op = false;
    int num_f = 0;
    int num_p = 0;
    // if the user did not provide any arguments, then print the help and exit
    if (argc <= 1) {
        print_help(argv[0]);
        return -1;
    }
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0) {
            print_help(argv[0]);
            return -1;
        } else if (strcmp(argv[i], "-p") == 0) {
            FILE_STORAGE_API_PRINTS_ENABLED = true;
            ++num_p;
        } else if (strcmp(argv[i], "-f") == 0) {
            VALIDATE_BINARY_ARG("-f");
            sockname = argv[i];
            ++num_f;
        } else if (strcmp(argv[i], "-w") == 0) {
            VALIDATE_BINARY_ARG("-w")
            writes_op = true;
        } else if (strcmp(argv[i], "-W") == 0) {
            VALIDATE_BINARY_ARG("-W")
            writes_op = true;
        } else if (strcmp(argv[i], "-D") == 0) {
            VALIDATE_BINARY_ARG("-D")
            expelled_dirname = argv[i];
        } else if (strcmp(argv[i], "-d") == 0) {
            VALIDATE_BINARY_ARG("-d")
            read_dirname = argv[i];
        } else if (strcmp(argv[i], "-r") == 0) {
            VALIDATE_BINARY_ARG("-r")
            reads_op = true;
        } else if (strcmp(argv[i], "-R") == 0) {
            // -R arg is optional
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                ++i;
            }
            reads_op = true;
        } else if (strcmp(argv[i], "-t") == 0) {
            VALIDATE_BINARY_ARG("-t")
            long res;
            int conversion_res = string_to_long(argv[i], &res);
            if (conversion_res == -1) {
                fprintf(stderr, "options -t is required to have an integer argument\n");
                return -1;
            }

        } else if (strcmp(argv[i], "-l") == 0) {
            VALIDATE_BINARY_ARG("-l")
        } else if (strcmp(argv[i], "-u") == 0) {
            VALIDATE_BINARY_ARG("-u")
        } else if (strcmp(argv[i], "-c") == 0) {
            VALIDATE_BINARY_ARG("-c")
        } else {
            fprintf(stderr, "unrecognized option %s\n", argv[i]);
            return -1;
        }
    }
    if (sockname == NULL) {
        fprintf(stderr, "option -f is required\n");
        return -1;
    }
    if (num_f > 1) {
        fprintf(stderr, "options -f can be specified only one time\n");
        return -1;
    }
    if (num_p > 1) {
        fprintf(stderr, "options -p can be specified only one time\n");
        return -1;
    }
    if (!writes_op && expelled_dirname != NULL) {
        fprintf(stderr, "options -D is required to be in conjunction with -W or -w\n");
        return -1;
    }
    if (!reads_op && read_dirname != NULL) {
        fprintf(stderr, "options -d is required to be in conjunction with -R or -r\n");
        return -1;
    }
    return 0;
}

int recursively_visit_dir_and_write_file(const char dirname[], long* max_n)
{
    if (*max_n != 0) {
        DIR* dir;
        if ((dir = opendir(dirname)) == NULL) {
            perror("opendir");
            return -1;
        }
        struct dirent* file;
        while ((errno = 0, file = readdir(dir)) != NULL) {
            // skip . and ..
            if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) {
                continue;
            }

            // calculate the absolute path
            size_t abs_path_len = strlen(file->d_name) + strlen(dirname) + 2;
            char* abs_path = malloc(sizeof(char) * abs_path_len);
            strncpy(abs_path, dirname, abs_path_len);
            strncat(abs_path, "/", abs_path_len);
            strncat(abs_path, file->d_name, abs_path_len);

            struct stat st;
            if (stat(abs_path, &st) == -1) {
                perror("stat");
                return -1;
            }
            if (S_ISDIR(st.st_mode)) {
                recursively_visit_dir_and_write_file(abs_path, max_n);
                if (*max_n == 0) {
                    free(abs_path);
                    closedir(dir);
                    return 0;
                }
            } else {
                // write the file to the server
                API_CALL(openFile(abs_path, O_CREATE), "openFile");
                API_CALL(writeFile(abs_path, expelled_dirname), "writeFile");
                API_CALL(closeFile(abs_path), "closeFile");
                if (*max_n > 0) {
                    (*max_n)--;
                }
            }

            free(abs_path);
            if (*max_n == 0) {
                closedir(dir);
                return 0;
            }
        }
        if (errno != 0) {
            perror("readdir");
        }
        if (closedir(dir) == -1) {
            perror("closedir");
            return -1;
        };
    }
    return 0;
}

static int run_commands(int argc, char* argv[])
{
    // Open the connection to the server
    struct timespec abstime;
    abstime.tv_sec = time(NULL) + CONNECTION_RETRY_MAX_TIME;
    int open_res = openConnection(sockname, CONNECTION_RETRY_INTERVAL, abstime);
    if (open_res == -1) {
        return -1;
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-w") == 0) {
            ++i;
            long max_n = -1;
            char* strtok_save = NULL;
            char* dirname = strtok_r(argv[i], ",", &strtok_save);
            char* n_arg = strtok_r(NULL, ",", &strtok_save);
            if (n_arg != NULL) {
                if (strlen(n_arg) < 3 || n_arg[0] != 'n' || n_arg[1] != '=') {
                    fprintf(stderr, "invalid argument %s\n", argv[i]);
                    continue;
                }
                int str_to_long_res = string_to_long(n_arg + 2, &max_n);
                if (str_to_long_res == -1) {
                    fprintf(stderr, "invalid argument %s\n", argv[i]);
                    continue;
                }
            }
            if (max_n <= 0) {
                max_n = -1;
            }
            int visit_res = recursively_visit_dir_and_write_file(dirname, &max_n);
            if (visit_res == -1) {
                return -1;
            }
        } else if (strcmp(argv[i], "-W") == 0) {
            ++i;
            char* strtok_save = NULL;
            char* tok = strtok_r(argv[i], ",", &strtok_save);
            while (tok) {
                API_CALL(openFile(tok, O_CREATE), "openFile");
                API_CALL(writeFile(tok, expelled_dirname), "writeFile");
                API_CALL(closeFile(tok), "closeFile");
                tok = strtok_r(NULL, ",", &strtok_save);
            }
        } else if (strcmp(argv[i], "-r") == 0) {
            ++i;
            char* strtok_save = NULL;
            char* tok = strtok_r(argv[i], ",", &strtok_save);
            while (tok) {
                void* buf;
                size_t size;
                API_CALL(openFile(tok, 0), "openFile");
                int read_res = readFile(tok, &buf, &size);
                API_CALL(closeFile(tok), "closeFile");
                if (read_res != -1 && read_dirname != NULL) {
                    int save_res = save_file_to_disk(read_dirname, tok, size, buf);
                    if (save_res == -1) {
                        return -1;
                    }
                }
                free(buf);
                tok = strtok_r(NULL, ",", &strtok_save);
            }
        } else if (strcmp(argv[i], "-R") == 0) {
            long n = 0;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                ++i;
                if (strlen(argv[i]) < 3 || argv[i][0] != 'n' || argv[i][1] != '=') {
                    fprintf(stderr, "invalid argument %s\n", argv[i]);
                    continue;
                }
                int str_to_long_res = string_to_long(argv[i] + 2, &n);
                if (str_to_long_res == -1 || n > INT_MAX || n < INT_MIN) {
                    fprintf(stderr, "invalid argument %s\n", argv[i]);
                    continue;
                }
            }
            API_CALL(readNFiles((int)n, read_dirname), "readNFiles");
        } else if (strcmp(argv[i], "-l") == 0) {
            ++i;
            char* strtok_save = NULL;
            char* tok = strtok_r(argv[i], ",", &strtok_save);
            while (tok) {
                API_CALL(openFile(tok, 0), "openFile");
                API_CALL(lockFile(tok), "lockFile");
                API_CALL(closeFile(tok), "closeFile");
                tok = strtok_r(NULL, ",", &strtok_save);
            }
        } else if (strcmp(argv[i], "-u") == 0) {
            ++i;
            char* strtok_save = NULL;
            char* tok = strtok_r(argv[i], ",", &strtok_save);
            while (tok) {
                API_CALL(openFile(tok, 0), "openFile");
                API_CALL(unlockFile(tok), "unlockFile");
                API_CALL(closeFile(tok), "closeFile");
                tok = strtok_r(NULL, ",", &strtok_save);
            }
        } else if (strcmp(argv[i], "-c") == 0) {
            ++i;
            char* strtok_save = NULL;
            char* tok = strtok_r(argv[i], ",", &strtok_save);
            while (tok) {
                API_CALL(openFile(tok, 0), "openFile");
                API_CALL(removeFile(tok), "removeFile");
                tok = strtok_r(NULL, ",", &strtok_save);
            }
        }
    }

    // close the connection to the server
    int close_res = closeConnection(sockname);
    if (close_res == -1) {
        return -1;
    }
    return 0;
}

int main(int argc, char* argv[])
{
    int validation_res = validate_args(argc, argv);
    if (validation_res == -1) {
        return -1;
    }
    int run_res = run_commands(argc, argv);
    if (run_res == -1) {
        return -1;
    }
    return 0;
}