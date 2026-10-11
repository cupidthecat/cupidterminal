#include "../common/test_common.h"

int main(void) {
    test_reset_terminal(3, 8);
    test_feed_string("A\xcc\x81\r\nB\r\nC\r\nD");
    kscrollup_n(1);
    test_assert_true(tgetscrolloffset() == 1 && tgetline(0)[0].u == 'A', "history setup");
    test_feed_string("\033[3J");
    kscrollup_n(100);
    test_assert_true(tgetscrolloffset() == 0, "ED3 must discard all saved lines");
    test_assert_true(tgetline(0)[0].u == 'B', "ED3 must preserve the live screen");
    test_assert_cursor(2, 1);
    test_feed_string("\r\nE");
    kscrollup_n(100);
    test_assert_true(tgetscrolloffset() == 1 && tgetline(0)[0].u == 'B', "new history after ED3");
    kscrollreset();
    test_feed_string("\033[2J");
    kscrollup_n(100);
    test_assert_true(tgetscrolloffset() == 1, "ED2 must keep saved lines");
    test_print_ok("screen/erase_history");
    return 0;
}
