#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "../../src/terminal_state.h"

void test_reset_terminal(int rows, int cols);
void test_feed_bytes(const uint8_t *bytes, size_t len);
void test_feed_string(const char *s);
void test_assert_true(int condition, const char *message);
void test_assert_cell(int row, int col, const char *glyph, uint32_t fg, uint32_t bg, uint16_t attrs);
void test_assert_cursor(int row, int col);
void test_assert_attrs(uint32_t fg, uint32_t bg, uint16_t attrs);
void test_assert_mode(const char *name, int actual, int expected);
char *test_snapshot_screen(void);
void test_free_snapshot(char *snapshot);
void test_print_ok(const char *suite_name);

#endif // TEST_COMMON_H
