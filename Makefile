OBJDIR=./bin
BINDIR=./bin
IDIR=./include
SRCDIR=./src
TESTDIR=./tests

CC = gcc
CFLAGS = -Wall -pedantic -std=gnu17 \
		-I$(IDIR)

_OBJ = configparser
OBJ = $(patsubst %,$(OBJDIR)/%,$(_OBJ).o)
TESTS = $(patsubst %,$(BINDIR)/%_test,$(_OBJ))
RUNTESTS = $(patsubst %,run_%_test,$(_OBJ))

.PHONY: all tests
all: $(OBJ)
tests: $(TESTS)

$(OBJDIR)/configparser.o: $(SRCDIR)/configparser.c $(IDIR)/configparser.h
	$(CC) $(CFLAGS) -c $< -o $@

# generic rule for all tests
$(TESTS): $(BINDIR)/%_test: $(TESTDIR)/%_test.c $(OBJDIR)/%.o
	$(CC) $(CFLAGS) $< -o $@

$(RUNTESTS): run_%_test: $(BINDIR)/%_test
	@echo "--> running test $<"
	@($< && echo "TEST PASSED") || echo "TEST FAILED"