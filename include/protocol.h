/*
    PROTOCOL SPECIFICATION FOR FILES CLIENT-SERVER COMUNICATION

    The first byte of the packet is always the opcode. Any eventual following data is
    dependent on the opcode:

    - COMP: the operation completed successfully.
        ------------
        | COMP (1) |
        ------------

    - ACK: this is like COMP but is used when the operation is performed
        across multiple packets. The meaning is that the packet is received, but
        it is expected more.
        -----------
        | ACK (1) |
        -----------

    - ERROR: there has been an error.
        The error is transimtted after the opcode and it is 1 byte long (interpreted as char)
        ----------------------------
        | ERROR (1) | err_code (1) |
        ----------------------------

    - DATA: the packet contains some binary data
        The first 8 bytes (interpreted as an unsigned long) are the data size.
        Then data_size bytes represent the actual data.
        -----------------------------------------------
        | DATA (1) | data_size (8) | data (data_size) |
        -----------------------------------------------

    - FILE: the packet contains a file
        The first 8 bytes (interpreted as an unsigned long) are the filename size.
        Then name_length bytes represent the name of the file represented as a
        *not null terminated* sequence of characters
        Then 8 bytes (interpreted as an unsigned long) are the data size.
        Then data_size bytes represent the actual data contained in the file.
        ------------------------------------------------------------------------------------------
        | FILE (1) | name_length (8) | filename (name_length) | data_size (8) | data (data_size) |
        ------------------------------------------------------------------------------------------
    
    - FILE_SEQUENCE: this packet indicates that will be sent multiple files as separate packets
        The first 8 bytes (interpreted as an unsigned long) indicates how many files
        will be sent.
        ---------------------------------
        | FILE_SEQUENCE (1) | count (8) |
        ---------------------------------

    - OPEN_FILE: the packet contains a request of file creation
        The first 8 bytes (interpreted as an unsigned long) are the filename size.
        Then name_length bytes represent the name of the file represented as a
        *not null terminated* sequence of characters
        Then 1 byte 1 byte represent the open flags (O_CREATE | O_LOCK)
        ------------------------------------------------------------------------
        | OPEN_FILE (1) | name_length (8) | filename (name_length) | flags (1) |
        ------------------------------------------------------------------------
        
    - CLOSE_FILE: the packet contains a request of file closing
        The first 8 bytes (interpreted as an unsigned long) are the filename size.
        Then name_length bytes represent the name of the file represented as a
        *not null terminated* sequence of characters
        -------------------------------------------------------------
        | CLOSE_FILE (1) | name_length (8) | filename (name_length) |
        -------------------------------------------------------------

    - READ_FILE: the packet contains a request of file read
        The first 8 bytes (interpreted as an unsigned long) are the filename size.
        Then name_length bytes represent the name of the file represented as a
        *not null terminated* sequence of characters
        ------------------------------------------------------------
        | READ_FILE (1) | name_length (8) | filename (name_length) |
        ------------------------------------------------------------
    
    - APPEND_TO_FILE the packet contains a request to append to a file some data
        The first 8 bytes (interpreted as an unsigned long) are the filename size.
        Then name_length bytes represent the name of the file represented as a
        *not null terminated* sequence of characters
        -----------------------------------------------------------------
        | APPEND_TO_FILE (1) | name_length (8) | filename (name_length) |
        -----------------------------------------------------------------

    - LOCK_FILE: the packet contains a request to lock a file
        The first 8 bytes (interpreted as an unsigned long) are the filename size.
        Then name_length bytes represent the name of the file represented as a
        *not null terminated* sequence of characters
        ------------------------------------------------------------
        | LOCK_FILE (1) | name_length (8) | filename (name_length) |
        ------------------------------------------------------------

    - UNLOCK_FILE: the packet contains a request to unlock a file
        The first 8 bytes (interpreted as an unsigned long) are the filename size.
        Then name_length bytes represent the name of the file represented as a
        *not null terminated* sequence of characters
        --------------------------------------------------------------
        | UNLOCK_FILE (1) | name_length (8) | filename (name_length) |
        --------------------------------------------------------------
*/

#include <stdlib.h>

enum opcodes {
    COMP,
    ACK,
    ERROR,
    DATA,
    FILE,
    FILE_SEQUENCE,
    OPEN_FILE,
    CLOSE_FILE,
    READ_FILE,
    APPEND_TO_FILE,
    LOCK_FILE,
    UNLOCK_FILE
};

int send_comp(int fd);
int send_ack(int fd);
int send_error(int fd, char ecode);
int send_data(int fd, size_t size, void* data);
int send_file(int fd, size_t filename_len, const char* filename, size_t data_len, void* data);
int send_file_sequence(int fd, unsigned long count);
int send_open_file(int fd, size_t filename_len, const char* filename);
int send_close_file(int fd, size_t filename_len, const char* filename);
int send_read_file(int fd, size_t filename_len, const char* filename);
int send_append_to_file(int fd, size_t filename_len, const char* filename);
int send_lock_file(int fd, size_t filename_len, const char* filename);
int send_unlock_file(int fd, size_t filename_len, const char* filename);

int receive_opcode(int fd, int* opcode);
int receive_error(int fd, char* ecode);
int receive_data(int fd, size_t* size, void** data);
int receive_file(int fd, size_t* filename_len, char** filename, size_t* data_len, void** data);
int receive_file_sequence(int fd, unsigned long* count);
int receive_open_file(int fd, size_t* filename_len, char** filename);
int receive_close_file(int fd, size_t* filename_len, char** filename);
int receive_read_file(int fd, size_t* filename_len, char** filename);
int receive_append_to_file(int fd, size_t* filename_len, char** filename);
int receive_lock_file(int fd, size_t* filename_len, char** filename);
int receive_unlock_file(int fd, size_t* filename_len, char** filename);