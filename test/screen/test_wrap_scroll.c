#include "../common/test_common.h"

int main(void) {
    test_reset_terminal(2, 5);
    test_feed_string("ABCDE");
    test_assert_cursor(0, 5);
    test_feed_string("F");
    test_assert_cell(0, 4, "E", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 0, "F", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 1);

    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\rZ");
    test_assert_cell(0, 0, "Z", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 4, "E", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 1);

    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\nX");
    test_assert_cell(1, 4, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 5);

    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\x1b[DZ");
    test_assert_cell(0, 4, "Z", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 5);

    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\tX");
    test_assert_cell(0, 4, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 5);

    test_reset_terminal(2, 5);
    test_feed_string("ABCDEFGHIJK");
    test_assert_cell(0, 0, "F", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 4, "J", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 0, "K", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 1);

    test_print_ok("screen/wrap_scroll");
    return 0;
}
