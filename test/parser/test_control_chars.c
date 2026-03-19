#include "../common/test_common.h"

int main(void) {
    test_reset_terminal(4, 16);
    test_feed_string("abc\rZ");
    test_assert_cell(0, 0, "Z", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, "b", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 2, "c", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_reset_terminal(4, 16);
    test_feed_string("a\tb");
    test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 8, "b", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_reset_terminal(4, 16);
    test_feed_string("abc\bX");
    test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, "b", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 2, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_feed_string("\a");
    test_assert_mode("bell_rung", term_state.bell_rung, 1);

    test_reset_terminal(3, 8);
    test_feed_string("A\nB");
    test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 1, "B", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 2);

    test_reset_terminal(2, 8);
    test_feed_string("\bX");
    test_assert_cell(0, 0, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 1);

    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\tX");
    test_assert_cell(0, 4, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 5);

    test_print_ok("parser/control_chars");
    return 0;
}
