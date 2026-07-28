# Makefile

CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c99 -O2 -fPIC -I/usr/include/X11 -I/usr/include/X11/Xft -I/usr/include/freetype2
LDFLAGS = -lX11 -lXft -lfreetype -lutf8proc -lfontconfig
TEST_CFLAGS = $(CFLAGS) -Itest/common -Itest

SRCS = src/xwin.c src/cupid.c src/pty.c
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

.PHONY: all clean test test-all test-parser test-screen test-utf8 test-pty test-x11 test-manual install install-terminfo check-no-x11

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man
TERMINFO_DIR = $(PREFIX)/share/terminfo

all: $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/
	install -d $(DESTDIR)$(MANDIR)/man1
	install -m 644 cupidterminal.1 $(DESTDIR)$(MANDIR)/man1/cupidterminal.1
	tic -sx -o $(DESTDIR)$(TERMINFO_DIR) terminfo/cupidterminal.ti

install-terminfo:
	tic -sx terminfo/cupidterminal.ti

src/config.h:
	cp src/config.def.h src/config.h

$(TARGET): src/config.h $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

build/%.o: src/%.c src/cupid.h src/pty.h src/xwin.h src/config.h
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_BIN_DIR):
	mkdir -p $(TEST_BIN_DIR)

$(TEST_COMMON_OBJ): $(TEST_COMMON_SRC) test/common/test_common.h src/cupid.h
	mkdir -p build
	$(CC) $(TEST_CFLAGS) -c $< -o $@

# cupid_test.o: cupid.c compiled without main() and argv globals (for unit tests)
build/cupid_test.o: src/cupid.c src/cupid.h src/xwin.h src/config.h
	mkdir -p build
	$(CC) $(TEST_CFLAGS) -DCUPID_NO_MAIN -c src/cupid.c -o $@

$(TEST_BIN_DIR)/parser_%: test/parser/%.c $(TEST_COMMON_OBJ) build/cupid_test.o build/pty.o src/config.h | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) $< $(TEST_COMMON_OBJ) build/cupid_test.o build/pty.o -o $@ -lutf8proc

$(TEST_BIN_DIR)/screen_%: test/screen/%.c $(TEST_COMMON_OBJ) build/cupid_test.o build/pty.o src/config.h | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) $< $(TEST_COMMON_OBJ) build/cupid_test.o build/pty.o -o $@ -lutf8proc

$(TEST_BIN_DIR)/utf8_%: test/utf8/%.c $(TEST_COMMON_OBJ) build/cupid_test.o build/pty.o src/config.h | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) $< $(TEST_COMMON_OBJ) build/cupid_test.o build/pty.o -o $@ -lutf8proc

$(TEST_BIN_DIR)/pty_%: test/pty/%.c build/pty.o src/config.h | $(TEST_BIN_DIR)
	$(CC) $(TEST_CFLAGS) $< build/pty.o -o $@

clean:
	rm -rf build $(TARGET)

check-no-x11:
	@if grep -nE 'Xlib|Xft|XEvent|XStoreName|XBell|XParse|XSet[A-Z]|XGet[A-Z]|XCreate[A-Z]|Display[ \t*]|Window[ \t*]|XColor|Xrender|Mod[0-9]Mask|ShiftMask|ControlMask|KeySym|XK_' src/cupid.c; then \
		echo "ERROR: Xlib symbols leaked into src/cupid.c"; exit 1; \
	fi
	@echo "OK: src/cupid.c is X11-clean"

test: check-no-x11 test-all test-x11

test-all: test-parser test-screen test-utf8 test-pty
	@echo "All automated test suites PASSED."

test-parser: $(PARSER_TEST_BINS)
	@set -e; for t in $(PARSER_TEST_BINS); do echo "Running $$t"; "$$t"; done

test-screen: $(SCREEN_TEST_BINS)
	@set -e; for t in $(SCREEN_TEST_BINS); do echo "Running $$t"; "$$t"; done

test-utf8: $(UTF8_TEST_BINS)
	@set -e; for t in $(UTF8_TEST_BINS); do echo "Running $$t"; "$$t"; done

test-pty: $(PTY_TEST_BINS)
	@set -e; for t in $(PTY_TEST_BINS); do echo "Running $$t"; "$$t"; done

test-x11: $(TARGET)
	@bash test/x11/test_focus_reporting.sh
	@bash test/x11/test_keyboard_input.sh
	@bash test/x11/test_term_environment.sh

test-manual:
	@echo "Manual test scripts:"
	@for s in $(MANUAL_TEST_SCRIPTS); do echo "  $$s"; done
