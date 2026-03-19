#include <stdint.h>

#include "../common/test_common.h"

int main(void) {
    static const char wide[] = "\xE7\x95\x8C";            // U+754C
    static const char combining[] = "\xCC\x81";          // U+0301
    static const char wide_combined[] = "\xE7\x95\x8C\xCC\x81";
    static const char box_h[] = "\xE2\x94\x80";         // U+2500
    static const char braille[] = "\xE2\xA0\x80";       // U+2800
    static const char gear[] = "\xE2\x9A\x99";          // U+2699

    test_reset_terminal(3, 8);
    test_feed_string("A");
    test_feed_string(wide);
    test_feed_string("B");
    test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, wide, COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 2, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_true(terminal_buffer[0][1].width == 2, "wide lead width mismatch");
    test_assert_true(terminal_buffer[0][2].is_continuation == 1, "wide continuation flag mismatch");
    test_assert_cell(0, 3, "B", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 4);

    test_reset_terminal(2, 4);
    test_feed_string("ABC");
    test_feed_string(wide);
    test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, "B", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 2, "C", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 0, wide, COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 1, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_true(terminal_buffer[1][1].is_continuation == 1, "wrapped wide continuation missing");
    test_assert_cursor(1, 2);

    test_reset_terminal(2, 8);
    test_feed_string("e");
    test_feed_string(combining);
    test_feed_string("x");
    test_assert_cell(0, 0, "e\xCC\x81", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, "x", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 2);

    test_reset_terminal(2, 8);
    test_feed_string(wide);
    test_feed_string(combining);
    test_assert_cell(0, 0, wide_combined, COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 2);

    test_reset_terminal(2, 8);
    test_feed_string(box_h);
    test_feed_string(braille);
    test_feed_string(gear);
    test_assert_cell(0, 0, box_h, COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, braille, COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 2, gear, COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 3);

    test_print_ok("utf8/wide_combining");
    return 0;
}
