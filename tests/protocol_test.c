#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "protocol.h"

int main(void)
{
    int fds[2];
    pipe(fds);
    char dummy_data[10] = { 0, 1, 0, 12, 126, 30, 46, 50, 0, 53 };
    char dummy_filename[6] = "AAAAA";

    struct packet send, recv;

    // TEST NIL
    clear_packet(&send);
    clear_packet(&recv);
    assert(send_packet(fds[1], &send) == -1);
    assert(errno == EINVAL);

    // TEST COMP
    clear_packet(&send);
    clear_packet(&recv);
    send.op = COMP;
    assert(send_packet(fds[1], &send) > 0);
    assert(receive_packet(fds[0], &recv) > 0);
    assert(recv.op == COMP);

    // TEST ACK
    clear_packet(&send);
    clear_packet(&recv);
    send.op = ACK;
    assert(send_packet(fds[1], &send) > 0);
    assert(receive_packet(fds[0], &recv) > 0);
    assert(recv.op == ACK);

    // TEST CLOSE_CONNECTION
    clear_packet(&send);
    clear_packet(&recv);
    send.op = CLOSE_CONNECTION;
    assert(send_packet(fds[1], &send) > 0);
    assert(receive_packet(fds[0], &recv) > 0);
    assert(recv.op == CLOSE_CONNECTION);

    // TEST ERROR
    clear_packet(&send);
    clear_packet(&recv);
    send.op = ERROR;
    send.err_code = 5;
    assert(send_packet(fds[1], &send) > 0);
    assert(receive_packet(fds[0], &recv) > 0);
    assert(recv.op == ERROR);
    assert(recv.err_code == 5);

    // TEST DATA
    clear_packet(&send);
    clear_packet(&recv);
    send.op = DATA;
    send.data_size = 10;
    send.data = dummy_data;
    assert(send_packet(fds[1], &send) > 0);
    assert(receive_packet(fds[0], &recv) > 0);
    assert(recv.op == DATA);
    assert(recv.data_size == 10);
    for (int i = 0; i < 10; ++i) {
        assert(((char*)recv.data)[i] == dummy_data[i]);
    }
    assert(destroy_packet(&recv) == 0);

    // TEST FILE_P
    clear_packet(&send);
    clear_packet(&recv);
    send.op = FILE_P;
    send.name_length = 5;
    send.filename = dummy_filename;
    send.data_size = 10;
    send.data = dummy_data;
    assert(send_packet(fds[1], &send) > 0);
    assert(receive_packet(fds[0], &recv) > 0);
    assert(recv.op == FILE_P);
    assert(recv.name_length == 5);
    assert(strcmp(recv.filename, "AAAAA") == 0);
    assert(recv.filename[recv.name_length] == '\0');
    assert(recv.data_size == 10);
    for (int i = 0; i < 10; ++i) {
        assert(((char*)recv.data)[i] == dummy_data[i]);
    }
    assert(destroy_packet(&recv) == 0);

    // TEST FILE_SEQUENCE
    clear_packet(&send);
    clear_packet(&recv);
    send.op = FILE_SEQUENCE;
    send.count = 42;
    assert(send_packet(fds[1], &send) > 0);
    assert(receive_packet(fds[0], &recv) > 0);
    assert(recv.op == FILE_SEQUENCE);
    assert(recv.count == 42);

    // TEST READ_N_FILES
    clear_packet(&send);
    clear_packet(&recv);
    send.op = READ_N_FILES;
    send.count = 42;
    assert(send_packet(fds[1], &send) > 0);
    assert(receive_packet(fds[0], &recv) > 0);
    assert(recv.op == READ_N_FILES);
    assert(recv.count == 42);

    // TEST OPEN_FILE
    clear_packet(&send);
    clear_packet(&recv);
    send.op = OPEN_FILE;
    send.name_length = 5;
    send.filename = dummy_filename;
    send.flags = 42;
    assert(send_packet(fds[1], &send) > 0);
    assert(receive_packet(fds[0], &recv) > 0);
    assert(recv.op == OPEN_FILE);
    assert(recv.name_length == 5);
    assert(strcmp(recv.filename, "AAAAA") == 0);
    assert(recv.filename[recv.name_length] == '\0');
    assert(recv.flags == 42);
    assert(destroy_packet(&recv) == 0);

    // TEST CLOSE_FILE
    clear_packet(&send);
    clear_packet(&recv);
    send.op = CLOSE_FILE;
    send.name_length = 5;
    send.filename = dummy_filename;
    assert(send_packet(fds[1], &send) > 0);
    assert(receive_packet(fds[0], &recv) > 0);
    assert(recv.op == CLOSE_FILE);
    assert(recv.name_length == 5);
    assert(strcmp(recv.filename, "AAAAA") == 0);
    assert(recv.filename[recv.name_length] == '\0');
    assert(destroy_packet(&recv) == 0);

    // TEST READ_FILE
    clear_packet(&send);
    clear_packet(&recv);
    send.op = READ_FILE;
    send.name_length = 5;
    send.filename = dummy_filename;
    assert(send_packet(fds[1], &send) > 0);
    assert(receive_packet(fds[0], &recv) > 0);
    assert(recv.op == READ_FILE);
    assert(recv.name_length == 5);
    assert(strcmp(recv.filename, "AAAAA") == 0);
    assert(recv.filename[recv.name_length] == '\0');
    assert(destroy_packet(&recv) == 0);

    // TEST APPEND_TO_FILE
    clear_packet(&send);
    clear_packet(&recv);
    send.op = APPEND_TO_FILE;
    send.name_length = 5;
    send.filename = dummy_filename;
    assert(send_packet(fds[1], &send) > 0);
    assert(receive_packet(fds[0], &recv) > 0);
    assert(recv.op == APPEND_TO_FILE);
    assert(recv.name_length == 5);
    assert(strcmp(recv.filename, "AAAAA") == 0);
    assert(recv.filename[recv.name_length] == '\0');
    assert(destroy_packet(&recv) == 0);

    // TEST LOCK_FILE
    clear_packet(&send);
    clear_packet(&recv);
    send.op = LOCK_FILE;
    send.name_length = 5;
    send.filename = dummy_filename;
    assert(send_packet(fds[1], &send) > 0);
    assert(receive_packet(fds[0], &recv) > 0);
    assert(recv.op == LOCK_FILE);
    assert(recv.name_length == 5);
    assert(strcmp(recv.filename, "AAAAA") == 0);
    assert(recv.filename[recv.name_length] == '\0');
    assert(destroy_packet(&recv) == 0);

    // TEST UNLOCK_FILE
    clear_packet(&send);
    clear_packet(&recv);
    send.op = UNLOCK_FILE;
    send.name_length = 5;
    send.filename = dummy_filename;
    assert(send_packet(fds[1], &send) > 0);
    assert(receive_packet(fds[0], &recv) > 0);
    assert(recv.op == UNLOCK_FILE);
    assert(recv.name_length == 5);
    assert(strcmp(recv.filename, "AAAAA") == 0);
    assert(recv.filename[recv.name_length] == '\0');
    assert(destroy_packet(&recv) == 0);
    return 0;
}