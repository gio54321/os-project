OBJDIR=./bin
BINDIR=./bin
IDIR=./include
SRCDIR=./src
TESTDIR=./tests

CC = gcc
CFLAGS = -std=c99 -Wall -pedantic -I$(IDIR) -g 
LIBS = -lpthread

_OBJ = configparser unbounded_shared_buffer protocol int_queue file_storage_internal\
	   utils logger thread_pool rw_lock compression server_worker file_storage_api
TEST_OBJ = configparser unbounded_shared_buffer protocol int_queue file_storage_internal\
	   utils logger thread_pool rw_lock compression
CONCURRENT_OBJ = unbounded_shared_buffer logger thread_pool rw_lock

OBJ = $(patsubst %,$(OBJDIR)/%.o,$(_OBJ))
TESTS = $(patsubst %,$(BINDIR)/%_test,$(TEST_OBJ))
RUNTESTS = $(patsubst %,run_%_test,$(TEST_OBJ))

CTESTS = $(patsubst %,$(BINDIR)/%_ctest,$(CONCURRENT_OBJ))
RUNCTESTS = $(patsubst %,run_%_ctest,$(CONCURRENT_OBJ))

.PHONY: all tests run-all-tests run-tests run-ctests clean test1 test2 test3

test1: all
	./scripts/test1.sh

test2: all
	./scripts/test2.sh

test3: all
	./scripts/test3.sh

all: $(OBJ) $(BINDIR)/server $(BINDIR)/client

clean:
	rm -f $(OBJ) $(TESTS) $(CTESTS)

run-all-tests: run-tests run-ctests
run-tests: tests $(RUNTESTS)
run-ctests: tests $(RUNCTESTS)
tests: $(TESTS)

$(OBJDIR)/configparser.o: $(SRCDIR)/configparser.c $(IDIR)/configparser.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/unbounded_shared_buffer.o: $(SRCDIR)/unbounded_shared_buffer.c $(IDIR)/unbounded_shared_buffer.h
	$(CC) $(CFLAGS) -c $< -o $@ $(LIBS)

$(OBJDIR)/protocol.o: $(SRCDIR)/protocol.c $(IDIR)/protocol.h $(OBJDIR)/utils.o
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/int_queue.o: $(SRCDIR)/int_queue.c $(IDIR)/int_queue.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/file_storage_internal.o: $(SRCDIR)/file_storage_internal.c $(IDIR)/file_storage_internal.h $(OBJDIR)/int_queue.o
	$(CC) $(CFLAGS) -c $< -o $@ $(LIBS)

$(OBJDIR)/utils.o: $(SRCDIR)/utils.c $(IDIR)/utils.h
	$(CC) $(CFLAGS) -c $< -o $@ $(LIBS)

$(OBJDIR)/logger.o: $(SRCDIR)/logger.c $(IDIR)/logger.h
	$(CC) $(CFLAGS) -c $< -o $@ $(LIBS)

$(OBJDIR)/thread_pool.o: $(SRCDIR)/thread_pool.c $(IDIR)/thread_pool.h
	$(CC) $(CFLAGS) -c $< -o $@ $(LIBS)

$(OBJDIR)/rw_lock.o: $(SRCDIR)/rw_lock.c $(IDIR)/rw_lock.h
	$(CC) $(CFLAGS) -c $< -o $@ $(LIBS)

$(OBJDIR)/compression.o: $(SRCDIR)/compression.c $(IDIR)/compression.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/server_worker.o: $(SRCDIR)/server_worker.c $(IDIR)/server_worker.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/server: $(SRCDIR)/server.c $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

$(OBJDIR)/file_storage_api.o: $(SRCDIR)/file_storage_api.c $(IDIR)/file_storage_api.h
	$(CC) $(CFLAGS) -c $< -o $@

# TODO separate client objs from server objs
$(OBJDIR)/client: $(SRCDIR)/client.c $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

# generic rule for all tests
$(TESTS): $(BINDIR)/%_test: $(TESTDIR)/%_test.c $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

$(CTESTS): $(BINDIR)/%_ctest: $(TESTDIR)/%_ctest.c $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

# the test fails if the test process returns a nonzero value or valgrind detects any leak
$(RUNTESTS): run_%_test: $(BINDIR)/%_test
	@tput setaf 7
	@echo "--> running test $<"
	@(valgrind --quiet --leak-check=full --error-exitcode=1 $< && (tput setaf 2; echo "TEST PASSED")) || (tput setaf 1; echo "TEST FAILED")
	@tput setaf 7

$(RUNCTESTS): run_%_ctest: $(BINDIR)/%_ctest
	@tput setaf 7
	@echo "--> running concurrency test $<"
	@($< && (tput setaf 2; echo "TEST PASSED")) || (tput setaf 1; echo "TEST FAILED")
	@tput setaf 7