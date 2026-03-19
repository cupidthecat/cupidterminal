#include <stdint.h>
#include <string.h>

#include "../common/test_common.h"

static void assert_payload(const uint8_t *actual, size_t actual_len, const char *expected, size_t expected_len, const char *message) {
    test_assert_true(actual_len == expected_len, message);
    test_assert_true(memcmp(actual, expected, expected_len) == 0, message);
}

int main(void) {
    uint8_t payload[64];
    size_t payload_len;

    static const uint8_t osc_part1[] = {0x1B, ']', '2', ';', 's', 'p'};
    static const uint8_t osc_part2[] = {'l', 'i', 't', 0x07};
    static const uint8_t osc52_part1[] = {0x1B, ']', '5', '2', ';', 'c', ';', 'd', '2', '9'};
    static const uint8_t osc52_part2[] = {'y', 'b', 'G', 'Q', '=', 0x1B, '\\'};

    test_reset_terminal(3, 6);
    test_feed_string("M");
    test_feed_string("\x1b[2;3H");
    test_feed_string("X");

    test_feed_string("\x1b[?1049h");
    test_assert_mode("alt_screen_active", term_state.alt_screen_active, 1);
    test_assert_cursor(0, 0);
    test_assert_cell(0, 0, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_feed_string("ALT");
    test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, "L", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 2, "T", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_feed_string("\x1b[?1049l");
    test_assert_mode("alt_screen_active", term_state.alt_screen_active, 0);
    test_assert_cell(0, 0, "M", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 2, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 3);

    test_feed_string("\x1b[?2004h");
    test_assert_mode("bracketed_paste_mode", term_state.bracketed_paste_mode, 1);
    payload_len = terminal_format_paste_payload((const uint8_t *)"abc", 3, term_state.bracketed_paste_mode, payload, sizeof(payload));
    assert_payload(payload, payload_len, "\x1b[200~abc\x1b[201~", 15, "bracketed paste payload mismatch");

    test_feed_string("\x1b[?2004l");
    test_assert_mode("bracketed_paste_mode", term_state.bracketed_paste_mode, 0);
    payload_len = terminal_format_paste_payload((const uint8_t *)"abc", 3, term_state.bracketed_paste_mode, payload, sizeof(payload));
    assert_payload(payload, payload_len, "abc", 3, "plain paste payload mismatch");

    test_feed_string("\x1b[?1000h\x1b[?1006h");
    test_assert_mode("mouse_reporting_basic", term_state.mouse_reporting_basic, 1);
    test_assert_mode("mouse_sgr_mode", term_state.mouse_sgr_mode, 1);
    test_feed_string("\x1b[?1000l\x1b[?1006l");
    test_assert_mode("mouse_reporting_basic", term_state.mouse_reporting_basic, 0);
    test_assert_mode("mouse_sgr_mode", term_state.mouse_sgr_mode, 0);

    test_feed_string("\x1b[?1h");
    test_assert_mode("application_cursor_keys", term_state.application_cursor_keys, 1);
    test_feed_string("\x1b[?1l");
    test_assert_mode("application_cursor_keys", term_state.application_cursor_keys, 0);

    test_feed_string("\x1b]0;title-one\x07");
    test_assert_true(strcmp(term_state.window_title, "title-one") == 0, "OSC 0 title mismatch");
    test_assert_mode("title_dirty", term_state.title_dirty, 1);

    term_state.title_dirty = 0;
    test_feed_string("\x1b]2;title-two\x1b\\");
    test_assert_true(strcmp(term_state.window_title, "title-two") == 0, "OSC 2 title mismatch");
    test_assert_mode("title_dirty", term_state.title_dirty, 1);

    term_state.title_dirty = 0;
    test_feed_bytes(osc_part1, sizeof(osc_part1));
    test_assert_mode("title_dirty", term_state.title_dirty, 0);
    test_feed_bytes(osc_part2, sizeof(osc_part2));
    test_assert_true(strcmp(term_state.window_title, "split") == 0, "split OSC title mismatch");
    test_assert_mode("title_dirty", term_state.title_dirty, 1);

    term_state.osc52_pending = 0;
    test_feed_string("\x1b]52;c;aGVsbG8=\x07");
    test_assert_mode("osc52_pending", term_state.osc52_pending, 1);
    test_assert_true(term_state.osc52_len == 5, "OSC 52 decoded length mismatch");
    test_assert_true(memcmp(term_state.osc52_buf, "hello", 5) == 0, "OSC 52 decoded payload mismatch");

    term_state.osc52_pending = 0;
    test_feed_bytes(osc52_part1, sizeof(osc52_part1));
    test_assert_mode("osc52_pending", term_state.osc52_pending, 0);
    test_feed_bytes(osc52_part2, sizeof(osc52_part2));
    test_assert_mode("osc52_pending", term_state.osc52_pending, 1);
    test_assert_true(term_state.osc52_len == 5, "split OSC 52 decoded length mismatch");
    test_assert_true(memcmp(term_state.osc52_buf, "world", 5) == 0, "split OSC 52 payload mismatch");

    test_print_ok("parser/tier3_modes");
    return 0;
}
