#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Provide config symbols for terminal_state (used when not linking with main.c) */
unsigned int tabspaces = 8;
char *vtiden = "\033[?6c";
int allowwindowops = 1;  /* enable OSC 52 for unit tests */

static void failf(const char *message) {
    fprintf(stderr, "TEST FAILURE: %s\n", message);
    exit(EXIT_FAILURE);
}

void test_reset_terminal(int rows, int cols) {
    initialize_terminal_state(&term_state);
    if (rows > 0 && cols > 0) {
        resize_terminal(rows, cols);
    }
    term_state.row = 0;
    term_state.col = 0;
    /* Clear screen so tests don't see leftover from previous test (same-size resize skips realloc) */
    static const char ed2[] = "\x1b[2J";
    terminal_consume_bytes((const uint8_t *)ed2, sizeof(ed2) - 1, &term_state, NULL, NULL);
    term_state.row = 0;
    term_state.col = 0;
}

void test_feed_bytes(const uint8_t *bytes, size_t len) {
    terminal_consume_bytes(bytes, len, &term_state, NULL, NULL);
}

void test_feed_string(const char *s) {
    if (!s) {
        return;
    }
    terminal_consume_bytes((const uint8_t *)s, strlen(s), &term_state, NULL, NULL);
}

void test_assert_true(int condition, const char *message) {
    if (!condition) {
        failf(message ? message : "assertion failed");
    }
}

void test_assert_cell(int row, int col, const char *glyph, uint32_t fg, uint32_t bg, uint16_t attrs) {
    const char *actual = terminal_buffer[row][col].c;
    const char *expected = glyph ? glyph : "";

    if (strcmp(actual, expected) != 0 ||
        terminal_buffer[row][col].fg != fg ||
        terminal_buffer[row][col].bg != bg ||
        terminal_buffer[row][col].attrs != attrs) {
        fprintf(stderr,
            "TEST FAILURE: cell[%d,%d] expected ('%s',%u,%u,%u) got ('%s',%u,%u,%u)\n",
            row,
            col,
            expected,
            fg,
            bg,
            attrs,
            actual,
            terminal_buffer[row][col].fg,
            terminal_buffer[row][col].bg,
            terminal_buffer[row][col].attrs);
        exit(EXIT_FAILURE);
    }
}

void test_assert_cursor(int row, int col) {
    if (term_state.row != row || term_state.col != col) {
        fprintf(stderr,
            "TEST FAILURE: cursor expected (%d,%d), got (%d,%d)\n",
            row,
            col,
            term_state.row,
            term_state.col);
        exit(EXIT_FAILURE);
    }
}

void test_assert_attrs(uint32_t fg, uint32_t bg, uint16_t attrs) {
    if (term_state.current_fg != fg || term_state.current_bg != bg || term_state.current_attrs != attrs) {
        fprintf(stderr,
            "TEST FAILURE: current attrs expected (%u,%u,%u), got (%u,%u,%u)\n",
            fg,
            bg,
            attrs,
            term_state.current_fg,
            term_state.current_bg,
            term_state.current_attrs);
        exit(EXIT_FAILURE);
    }
}

void test_assert_mode(const char *name, int actual, int expected) {
    if (actual != expected) {
        fprintf(stderr,
            "TEST FAILURE: mode %s expected %d, got %d\n",
            name ? name : "(unnamed)",
            expected,
            actual);
        exit(EXIT_FAILURE);
    }
}

char *test_snapshot_screen(void) {
    size_t stride = (size_t)term_cols + 1;
    size_t total = ((size_t)term_rows * stride) + 1;
    char *snapshot = calloc(total, 1);
    size_t out = 0;

    if (!snapshot) {
        failf("snapshot allocation failed");
    }

    for (int r = 0; r < term_rows; r++) {
        for (int c = 0; c < term_cols; c++) {
            const char *g = terminal_buffer[r][c].c;
            char ch = ' ';
            if (g[0] != '\0') {
                if (((unsigned char)g[0]) < 0x80 && g[1] == '\0') {
                    ch = g[0];
                } else {
                    ch = '*';
                }
            }
            snapshot[out++] = ch;
        }
        snapshot[out++] = '\n';
    }
    snapshot[out] = '\0';
    return snapshot;
}

void test_free_snapshot(char *snapshot) {
    free(snapshot);
}

void test_print_ok(const char *suite_name) {
    printf("PASS: %s\n", suite_name ? suite_name : "suite");
}
