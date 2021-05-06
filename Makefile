OBJDIR=./bin
BINDIR=./bin
IDIR=./include
SRCDIR=./src
TESTDIR=./tests

CC = gcc
CFLAGS = -Wall -pedantic -std=gnu17 \
		-I$(IDIR) -g

_OBJ = configparser
OBJ = $(patsubst %,$(OBJDIR)/%,$(_OBJ).o)
TESTS = $(patsubst %,$(BINDIR)/%_test,$(_OBJ))
RUNTESTS = $(patsubst %,run_%_test,$(_OBJ))

.PHONY: all tests run-tests

all: $(OBJ)

run-tests: tests $(RUNTESTS)
tests: $(TESTS)

$(OBJDIR)/configparser.o: $(SRCDIR)/configparser.c $(IDIR)/configparser.h
	$(CC) $(CFLAGS) -c $< -o $@

# generic rule for all tests
$(TESTS): $(BINDIR)/%_test: $(TESTDIR)/%_test.c $(OBJDIR)/%.o
	$(CC) $(CFLAGS) $^ -o $@


# the test fails if the test process returns a nonzero value or valgrind detects any leak
$(RUNTESTS): run_%_test: $(BINDIR)/%_test
	@echo "--> running test $<"
	@(valgrind --quiet --leak-check=full --error-exitcode=1 $< && echo "TEST PASSED") || echo "TEST FAILED"