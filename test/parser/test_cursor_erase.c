#include "../common/test_common.h"

int main(void) {
    test_reset_terminal(5, 12);
    test_feed_string("hello\x1b[2;5H@");
    test_assert_cell(0, 0, "h", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 4, "@", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 5);

    test_feed_string("\x1b[2J");
    test_assert_cell(0, 0, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 4, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 5);

    test_reset_terminal(3, 6);
    test_feed_string("111111222222333333");
    test_feed_string("\x1b[2;3H\x1b[J");
    test_assert_cell(0, 0, "1", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 0, "2", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 1, "2", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 2, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(2, 0, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 2);

    test_reset_terminal(3, 6);
    test_feed_string("111111222222333333");
    test_feed_string("\x1b[2;3H\x1b[1J");
    test_assert_cell(0, 0, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 0, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 2, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 3, "2", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(2, 0, "3", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 2);

    test_reset_terminal(2, 6);
    test_feed_string("abcdef");
    test_feed_string("\x1b[1;4H\x1b[K");
    test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 2, "c", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 3, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 5, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 3);

    test_reset_terminal(2, 6);
    test_feed_string("abcdef");
    test_feed_string("\x1b[1;4H\x1b[1K");
    test_assert_cell(0, 0, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 3, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 4, "e", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 5, "f", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 3);

    test_reset_terminal(2, 6);
    test_feed_string("abcdef");
    test_feed_string("\x1b[1;4H\x1b[2K");
    test_assert_cell(0, 0, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 5, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 3);

    test_reset_terminal(5, 12);
    test_feed_string("\x1b[3;4H\x1b[A\x1b[D#");
    test_assert_cell(1, 2, "#", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 3);

    test_print_ok("parser/cursor_erase");
    return 0;
}
