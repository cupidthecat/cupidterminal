/*
 * Tier 2: ICH, DCH, IL, DL tests.
 */
#include "../common/test_common.h"

int main(void) {
    /* ICH: Insert 1 blank at cursor */
    test_reset_terminal(2, 8);
    test_feed_string("abcdef\x1b[1;4H\x1b[@X");
    test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 3, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 4, "d", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 5, "e", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 6, "f", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 4);

    /* DCH: Delete 1 char at cursor; cursor stays, next char overwrites shifted content */
    test_reset_terminal(2, 8);
    test_feed_string("abcdef\x1b[1;4H\x1b[P");
    test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 3, "e", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 4, "f", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 3);
    test_feed_string("X");
    test_assert_cell(0, 3, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 4, "f", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* IL: Insert 1 line at cursor */
    test_reset_terminal(4, 6);
    test_feed_string("111111222222333333444444\x1b[2;1H\x1b[L@");
    test_assert_cell(0, 0, "1", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 0, "@", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(2, 0, "2", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(3, 0, "3", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* DL: Delete 1 line at cursor; line 2 removed, 3->1, 4->2, row 3 cleared */
    test_reset_terminal(4, 6);
    test_feed_string("111111222222333333444444\x1b[2;1H\x1b[M");
    test_assert_cell(0, 0, "1", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 0, "3", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(2, 0, "4", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(2, 5, "4", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(3, 0, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 0);

    test_print_ok("parser/tier2_insert_delete");
    return 0;
}
