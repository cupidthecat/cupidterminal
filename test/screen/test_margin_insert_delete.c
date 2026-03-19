#include "../common/test_common.h"

static void fill_rows_6x4(void) {
    test_feed_string("111122223333444455556666");
}

int main(void) {
    test_reset_terminal(6, 4);
    fill_rows_6x4();
    test_feed_string("\x1b[2;5r\x1b[3;1H\x1b[L");

    test_assert_cell(0, 0, "1", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 0, "2", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(2, 0, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(3, 0, "3", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(4, 0, "4", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(5, 0, "6", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_reset_terminal(6, 4);
    fill_rows_6x4();
    test_feed_string("\x1b[2;5r\x1b[3;1H\x1b[M");

    test_assert_cell(0, 0, "1", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 0, "2", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(2, 0, "4", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(3, 0, "5", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(4, 0, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(5, 0, "6", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_print_ok("screen/margin_insert_delete");
    return 0;
}