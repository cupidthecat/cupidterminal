# Makefile

CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c99 -O2 -fPIC -I/usr/include/X11 -I/usr/include/X11/Xft -I/usr/include/freetype2
LDFLAGS = -lX11 -lXft -lfreetype -lutf8proc -lfontconfig
TEST_CFLAGS = $(CFLAGS) -Itest/common -Itest

SRCS = src/main.c src/draw.c src/input.c src/terminal_state.c src/pty_session.c
OBJS = $(SRCS:src/%.c=build/%.o)
TARGET = cupidterminal

TEST_BIN_DIR = build/tests
TEST_COMMON_SRC = test/common/test_common.c
TEST_COMMON_OBJ = build/test_common.o

PARSER_TEST_SRCS := $(wildcard test/parser/*.c)
SCREEN_TEST_SRCS := $(wildcard test/screen/*.c)
UTF8_TEST_SRCS := $(wildcard test/utf8/*.c)
PTY_TEST_SRCS := $(wildcard test/pty/*.c)
MANUAL_TEST_SCRIPTS := $(wildcard test/manual/*.sh)

PARSER_TEST_BINS := $(patsubst test/parser/%.c,$(TEST_BIN_DIR)/parser_%,$(PARSER_TEST_SRCS))
SCREEN_TEST_BINS := $(patsubst test/screen/%.c,$(TEST_BIN_DIR)/screen_%,$(SCREEN_TEST_SRCS))
UTF8_TEST_BINS := $(patsubst test/utf8/%.c,$(TEST_BIN_DIR)/utf8_%,$(UTF8_TEST_SRCS))
PTY_TEST_BINS := $(patsubst test/pty/%.c,$(TEST_BIN_DIR)/pty_%,$(PTY_TEST_SRCS))

.PHONY: all clean test test-all test-parser test-screen test-utf8 test-pty test-manual install install-terminfo

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

all: $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/

# Install terminfo so TERM=cupidterminal-256color works. Run: make install-terminfo
install-terminfo:
	tic terminfo/cupidterminal.ti

src/config.h:
	cp src/config.def.h src/config.h

$(TARGET): src/config.h $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

build/%.o: src/%.c src/config.h
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_BIN_DIR):
	mkdir -p $(TEST_BIN_DIR)

$(TEST_COMMON_OBJ): $(TEST_COMMON_SRC) test/common/test_common.h src/terminal_state.h
	mkdir -p build
	$(CC) $(TEST_CFLAGS) -c $< -o $@

$(TEST_BIN_DIR)/parser_%: test/parser/%.c $(TEST_COMMON_OBJ) build/terminal_state.o src/config.h | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) $< $(TEST_COMMON_OBJ) build/terminal_state.o -o $@ -lutf8proc

$(TEST_BIN_DIR)/screen_%: test/screen/%.c $(TEST_COMMON_OBJ) build/terminal_state.o src/config.h | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) $< $(TEST_COMMON_OBJ) build/terminal_state.o -o $@ -lutf8proc

$(TEST_BIN_DIR)/utf8_%: test/utf8/%.c $(TEST_COMMON_OBJ) build/terminal_state.o src/config.h | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) $< $(TEST_COMMON_OBJ) build/terminal_state.o -o $@ -lutf8proc

$(TEST_BIN_DIR)/pty_%: test/pty/%.c build/pty_session.o src/config.h | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) $< build/pty_session.o -o $@

clean:
	rm -rf build $(TARGET)

test: test-all

test-all: test-parser test-screen test-utf8 test-pty
	@echo "All automated test suites PASSED."

test-parser: $(PARSER_TEST_BINS)
	@for t in $(PARSER_TEST_BINS); do echo "Running $$t"; "$$t"; done

test-screen: $(SCREEN_TEST_BINS)
	@for t in $(SCREEN_TEST_BINS); do echo "Running $$t"; "$$t"; done

test-utf8: $(UTF8_TEST_BINS)
	@for t in $(UTF8_TEST_BINS); do echo "Running $$t"; "$$t"; done

test-pty: $(PTY_TEST_BINS)
	@for t in $(PTY_TEST_BINS); do echo "Running $$t"; "$$t"; done

test-manual:
	@echo "Manual test scripts:"
	@for s in $(MANUAL_TEST_SCRIPTS); do echo "  $$s"; done
