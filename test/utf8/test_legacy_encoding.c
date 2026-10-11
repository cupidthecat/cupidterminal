#include "../common/test_common.h"

int main(void) {
    test_reset_terminal(3, 8);
    test_feed_string("\033%@\xe9" "A");
    test_assert_cell(0, 0, "\xc3\xa9", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_feed_string("\033%G\xc3\xa9");
    test_assert_cell(0, 2, "\xc3\xa9", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_reset_terminal(3, 8);
    test_feed_string("\033%@\xff\xa3");
    test_assert_cell(0, 0, "\xc3\xbf", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, "\xc2\xa3", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_print_ok("utf8/legacy_encoding");
    return 0;
}
