#include <string.h>

#include "../common/test_common.h"

typedef struct {
    uint8_t bytes[64];
    size_t len;
} ResponseCapture;

static void capture_response(const uint8_t *bytes, size_t len, void *ctx) {
    ResponseCapture *cap = (ResponseCapture *)ctx;

    if (!cap) {
        return;
    }

    if (!bytes || len == 0) {
        cap->len = 0;
        return;
    }

    if (len > sizeof(cap->bytes)) {
        len = sizeof(cap->bytes);
    }
    memcpy(cap->bytes, bytes, len);
    cap->len = len;
}

static void query_with_capture(const char *seq, ResponseCapture *cap) {
    cap->len = 0;
    terminal_consume_bytes((const uint8_t *)seq, strlen(seq), &term_state, capture_response, cap);
}

static void assert_response(const ResponseCapture *cap, const char *expected, size_t expected_len, const char *msg) {
    test_assert_true(cap->len == expected_len, msg);
    test_assert_true(memcmp(cap->bytes, expected, expected_len) == 0, msg);
}

int main(void) {
    ResponseCapture cap = {{0}, 0};
    static const char utf8_gear[] = "\xE2\x9A\x99";   /* contains 0x9A */
    static const char utf8_block[] = "\xE2\x96\x85";  /* contains 0x85 */
    static const char utf8_elem[] = "\xE2\x88\x88";   /* contains 0x88 */

    test_reset_terminal(4, 8);
    test_feed_string("\x1b[2;3H");
    query_with_capture("\x1b[5n", &cap);
    assert_response(&cap, "\x1b[0n", 4, "DSR 5n response mismatch");
    query_with_capture("\x1b[6n", &cap);
    assert_response(&cap, "\x1b[2;3R", 6, "DSR 6n CPR response mismatch");

    test_reset_terminal(2, 5);
    test_feed_string("ABCDE");
    query_with_capture("\x1b[6n", &cap);
    assert_response(&cap, "\x1b[1;5R", 6, "DSR 6n should clamp pending-wrap CPR column");

    test_reset_terminal(2, 5);
    query_with_capture("\x1b[c", &cap);
    assert_response(&cap, "\x1b[?6c", 5, "DA primary response mismatch");
    query_with_capture("\x1b[0c", &cap);
    assert_response(&cap, "\x1b[?6c", 5, "DA 0c response mismatch");

    /* UTF-8 continuation bytes must not be treated as standalone C1 controls. */
    test_reset_terminal(2, 8);
    cap.len = 0;
    terminal_consume_bytes((const uint8_t *)utf8_gear, strlen(utf8_gear), &term_state, capture_response, &cap);
    test_assert_true(cap.len == 0, "UTF-8 continuation 0x9A triggered unexpected DECID response");
    test_assert_cell(0, 0, utf8_gear, COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_reset_terminal(2, 8);
    cap.len = 0;
    terminal_consume_bytes((const uint8_t *)utf8_block, strlen(utf8_block), &term_state, capture_response, &cap);
    test_assert_true(cap.len == 0, "UTF-8 continuation 0x85 triggered unexpected NEL handling");
    test_assert_cell(0, 0, utf8_block, COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_reset_terminal(2, 8);
    cap.len = 0;
    terminal_consume_bytes((const uint8_t *)utf8_elem, strlen(utf8_elem), &term_state, capture_response, &cap);
    test_assert_true(cap.len == 0, "UTF-8 continuation 0x88 triggered unexpected HTS handling");
    test_assert_cell(0, 0, utf8_elem, COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_reset_terminal(2, 5);
    test_assert_mode("cursor_visible", term_state.cursor_visible, 1);
    test_feed_string("\x1b[?25l");
    test_assert_mode("cursor_visible", term_state.cursor_visible, 0);
    test_feed_string("\x1b[?1;25h");
    test_assert_mode("cursor_visible", term_state.cursor_visible, 1);
    test_feed_string("\x1b[?1;25l");
    test_assert_mode("cursor_visible", term_state.cursor_visible, 0);

    test_reset_terminal(4, 8);
    test_feed_string("\x1b[31;1m\x1b[2;3H\x1b[s\x1b[0m\x1b[1;1HX\x1b[uY");
    test_assert_cell(1, 2, "Y", 1, COLOR_DEFAULT_BG, ATTR_BOLD);
    test_assert_cursor(1, 3);
    test_assert_attrs(1, COLOR_DEFAULT_BG, ATTR_BOLD);

    test_reset_terminal(4, 8);
    test_feed_string("\x1b[34m\x1b[4;8H\x1b" "7");
    resize_terminal(2, 3);
    test_feed_string("\x1b" "8Z");
    test_assert_cell(1, 2, "Z", 4, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 3);
    test_assert_attrs(4, COLOR_DEFAULT_BG, 0);

    test_reset_terminal(2, 5);
    test_feed_string("\x1b[?7l");
    test_assert_mode("autowrap_mode", term_state.autowrap_mode, 0);
    test_feed_string("ABCDE");
    test_assert_cursor(0, 4);
    test_feed_string("Z");
    test_assert_cell(0, 4, "Z", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 4);
    test_feed_string("\x1b[?7h");
    test_assert_mode("autowrap_mode", term_state.autowrap_mode, 1);
    test_feed_string("Y");
    test_assert_cursor(0, 5);
    test_feed_string("X");
    test_assert_cell(1, 0, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_reset_terminal(6, 6);
    test_feed_string("\x1b[2;5r");
    test_feed_string("\x1b[?6h");
    test_assert_mode("origin_mode", term_state.origin_mode, 1);
    test_assert_cursor(1, 0);
    test_feed_string("\x1b[1;1H@");
    test_assert_cell(1, 0, "@", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_feed_string("\x1b[4;6H#");
    test_assert_cell(4, 5, "#", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_feed_string("\x1b[?6l");
    test_assert_mode("origin_mode", term_state.origin_mode, 0);
    test_assert_cursor(0, 0);

    test_reset_terminal(1, 6);
    test_feed_string("ABCD");
    test_feed_string("\x1b[1;2H\x1b[4hX");
    test_assert_mode("insert_mode", term_state.insert_mode, 1);
    test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 2, "B", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 3, "C", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 4, "D", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_feed_string("\x1b[4l");
    test_assert_mode("insert_mode", term_state.insert_mode, 0);

    test_print_ok("parser/tier2_modes_responses");
    return 0;
}