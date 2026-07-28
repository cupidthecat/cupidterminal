#include <stdlib.h>
#include <string.h>

#include "../common/test_common.h"

static void assert_selection(const char *expected, const char *message) {
    char *actual = getsel();
    test_assert_true(actual != NULL, message);
    test_assert_true(strcmp(actual, expected) == 0, message);
    free(actual);
}

int main(void) {
    test_reset_terminal(2, 6);
    test_feed_string("ABCDE");
    selstart(0, 0, 0, SEL_REGULAR);
    selrelease(4, 0);
    assert_selection("ABCDE",
        "partial final line selection must not gain a newline");

    test_reset_terminal(2, 4);
    test_feed_string("ABCDE");
    selstart(0, 0, 0, SEL_REGULAR);
    selrelease(0, 1);
    assert_selection("ABCDE",
        "selection across a soft wrap must not insert a newline");

    test_reset_terminal(2, 4);
    test_feed_string("ABCD\r\nEFGH");
    selstart(1, 0, 0, SEL_RECTANGULAR);
    selrelease(2, 1);
    assert_selection("BC\nFG",
        "rectangular selection must follow st newline rules");
    test_assert_true(selected(1, 0) && selected(2, 1) && !selected(0, 1),
        "selected() must interpret coordinates as column, row");

    test_reset_terminal(2, 5);
    test_feed_string("hello");
    selstart(2, 0, SNAP_WORD, SEL_REGULAR);
    selrelease(2, 0);
    assert_selection("hello",
        "word snap must expand to the complete word");

    test_reset_terminal(2, 5);
    test_feed_string("hello");
    selstart(0, 0, 0, SEL_REGULAR);
    selrelease(0, 0);
    test_feed_string("\033[?1049h");
    test_assert_true(!selected(0, 0),
        "primary selection must not appear on the alternate screen");
    test_feed_string("\033[?1049l");
    test_assert_true(selected(0, 0),
        "primary selection must reappear after leaving the alternate screen");

    test_reset_terminal(3, 5);
    test_feed_string("a\xCC\x81\r\nB\r\nC\r\nD");
    kscrollup_n(1);
    selstart(0, 0, 0, SEL_REGULAR);
    selrelease(0, 0);
    assert_selection("a\xCC\x81",
        "scrollback selection must copy the displayed historical cluster");

    test_print_ok("screen/selection_st_parity");
    return 0;
}
