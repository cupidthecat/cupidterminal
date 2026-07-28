#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * No-op win.h stubs for unit tests.
 * st.c now calls xsettitle/xbell at parse time; tests must satisfy the
 * linker without pulling in X11.  These are intentionally silent.
 */
void xbell(void) {}
void xsettitle(char *p) { (void)p; }
void xsetsel(char *str) { (void)str; }

/* Provide config symbols for cupid.c when building without its main(). */
unsigned int tabspaces = 8;
char *vtiden = "\033[?6c";
int allowwindowops = 1;  /* enable OSC 52 for unit tests */
int allowaltscreen = 1;
char *utmp = NULL;
char *scroll = NULL;
char *stty_args = "stty raw pass8 nl -echo -iexten -cstopb 38400";

static void failf(const char *message) {
    fprintf(stderr, "TEST FAILURE: %s\n", message);
    exit(EXIT_FAILURE);
}

void test_reset_terminal(int rows, int cols) {
    tnew(&term);
    if (rows > 0 && cols > 0) {
        tresize(rows, cols);
    }
    term.row = 0;
    term.col = 0;
    /* Clear screen so tests don't see leftover from previous test (same-size resize skips realloc) */
    static const char ed2[] = "\x1b[2J";
    twrite((const uint8_t *)ed2, sizeof(ed2) - 1, &term, NULL, NULL);
    term.row = 0;
    term.col = 0;
}

void test_feed_bytes(const uint8_t *bytes, size_t len) {
    twrite(bytes, len, &term, NULL, NULL);
}

void test_feed_string(const char *s) {
    if (!s) {
        return;
    }
    twrite((const uint8_t *)s, strlen(s), &term, NULL, NULL);
}

void test_assert_true(int condition, const char *message) {
    if (!condition) {
        failf(message ? message : "assertion failed");
    }
}

void test_assert_cell(int row, int col, const char *glyph, uint32_t fg, uint32_t bg, uint16_t mode) {
    char cluster[64];
    term_render_cluster(row, col, cluster, sizeof cluster);
    const char *actual = cluster;
    const char *expected = glyph ? glyph : "";

    if (strcmp(actual, expected) != 0 ||
        term_lines[row].line[col].fg != fg ||
        term_lines[row].line[col].bg != bg ||
        term_lines[row].line[col].mode != mode) {
        fprintf(stderr,
            "TEST FAILURE: cell[%d,%d] expected ('%s',%u,%u,%u) got ('%s',%u,%u,%u)\n",
            row,
            col,
            expected,
            fg,
            bg,
            mode,
            actual,
            term_lines[row].line[col].fg,
            term_lines[row].line[col].bg,
            term_lines[row].line[col].mode);
        exit(EXIT_FAILURE);
    }
}

void test_assert_cursor(int row, int col) {
    if (term.row != row || term.col != col) {
        fprintf(stderr,
            "TEST FAILURE: cursor expected (%d,%d), got (%d,%d)\n",
            row,
            col,
            term.row,
            term.col);
        exit(EXIT_FAILURE);
    }
}

void test_assert_attrs(uint32_t fg, uint32_t bg, uint16_t mode) {
    if (term.current_fg != fg || term.current_bg != bg || term.current_mode != mode) {
        fprintf(stderr,
            "TEST FAILURE: current mode expected (%u,%u,%u), got (%u,%u,%u)\n",
            fg,
            bg,
            mode,
            term.current_fg,
            term.current_bg,
            term.current_mode);
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
            char cluster[64];
            size_t n = term_render_cluster(r, c, cluster, sizeof cluster);
            char ch = ' ';
            if (n > 0) {
                if (((unsigned char)cluster[0]) < 0x80 && cluster[1] == '\0') {
                    ch = cluster[0];
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
