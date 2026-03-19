/*
 * DECSTBM scroll region and margin-scoped regression tests.
 */
#include "../common/test_common.h"

int main(void) {
    /* DECSTBM is non-private CSI r. Setting margins homes cursor to 1;1. */
    test_reset_terminal(6, 5);
    test_feed_string("T");
    test_feed_string("\x1b[6;1HB");
    test_feed_string("\x1b[2;4r");
    test_assert_cursor(0, 0);

    test_feed_string("\x1b[2;1H11111\x1b[3;1H22222\x1b[4;1H33333");
    test_feed_string("\x1b[4;1H\n");

    test_assert_cell(0, 0, "T", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 0, "2", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(2, 0, "3", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(3, 0, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(5, 0, "B", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(3, 0);

    /* Reset margins with CSI r and verify full-screen scroll resumes. */
    test_feed_string("\x1b[r");
    test_assert_cursor(0, 0);
    test_feed_string("\x1b[6;1H\n");
    test_assert_cell(4, 0, "B", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(5, 0, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(5, 0);

    test_print_ok("screen/scroll_region");
    return 0;
}
