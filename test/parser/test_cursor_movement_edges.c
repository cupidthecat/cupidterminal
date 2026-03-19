#include "../common/test_common.h"

int main(void) {
    test_reset_terminal(4, 5);

    test_feed_string("\x1b[;3H@");
    test_assert_cell(0, 2, "@", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 3);

    test_feed_string("\x1b[2;2f#");
    test_assert_cell(1, 1, "#", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 2);

    test_feed_string("\x1b[4;5H");
    test_assert_cursor(3, 4);

    test_feed_string("\x1b[0A");
    test_assert_cursor(2, 4);

    test_feed_string("\x1b[0D");
    test_assert_cursor(2, 3);

    test_feed_string("\x1b[0C");
    test_assert_cursor(2, 4);

    test_feed_string("\x1b[0B");
    test_assert_cursor(3, 4);

    test_feed_string("\x1b[999A");
    test_assert_cursor(0, 4);

    test_feed_string("\x1b[999D");
    test_assert_cursor(0, 0);

    test_feed_string("\x1b[999;999H!");
    test_assert_cell(3, 4, "!", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(3, 5);

    test_print_ok("parser/cursor_movement_edges");
    return 0;
}
