OBJDIR=./bin
BINDIR=./bin
IDIR=./include
SRCDIR=./src
TESTDIR=./tests

CC = gcc
CFLAGS = -Wall -pedantic -std=gnu17 \
		-I$(IDIR) -g 
LIBS = -lpthread

_OBJ = configparser unbounded_shared_buffer protocol int_queue
OBJ = $(patsubst %,$(OBJDIR)/%.o,$(_OBJ))
TESTS = $(patsubst %,$(BINDIR)/%_test,$(_OBJ))
RUNTESTS = $(patsubst %,run_%_test,$(_OBJ))

.PHONY: all tests run-tests clean

all: $(OBJ)

clean:
	rm -f $(OBJ) $(TESTS)

run-tests: tests $(RUNTESTS)
tests: $(TESTS)

$(OBJDIR)/configparser.o: $(SRCDIR)/configparser.c $(IDIR)/configparser.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/unbounded_shared_buffer.o: $(SRCDIR)/unbounded_shared_buffer.c $(IDIR)/unbounded_shared_buffer.h
	$(CC) $(CFLAGS) -c $< -o $@ $(LIBS)

$(OBJDIR)/protocol.o: $(SRCDIR)/protocol.c $(IDIR)/protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/int_queue.o: $(SRCDIR)/int_queue.c $(IDIR)/int_queue.h
	$(CC) $(CFLAGS) -c $< -o $@

# generic rule for all tests
$(TESTS): $(BINDIR)/%_test: $(TESTDIR)/%_test.c $(OBJDIR)/%.o
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)


# the test fails if the test process returns a nonzero value or valgrind detects any leak
$(RUNTESTS): run_%_test: $(BINDIR)/%_test
	@echo "--> running test $<"
	@(valgrind --quiet --leak-check=full --error-exitcode=1 $< && (tput setaf 2; echo "TEST PASSED")) || (tput setaf 1; echo "TEST FAILED")
	@tput setaf 7