#include "test_common.h"

int main(void) {
    test_reset_terminal(4, 12);
    selstart(0, 0, SNAP_WORD, SEL_REGULAR);
    test_feed_string("\033[?5;1004h\033[5 q\033[12l\033[20h\033%@\033c");
    test_assert_true(!term.echo_mode, "RIS restores remote echo");
    test_assert_true(!term.lnm_mode, "RIS restores newline mode");
    test_assert_true(term.utf8_mode, "RIS restores UTF-8 decoding");
    test_assert_true(!term.focus_mode, "RIS stops focus reports");
    test_assert_true(!term.screen_reverse, "RIS restores normal screen colors");
    test_assert_true(term.cursorshape == 0, "RIS restores default cursor");
    test_assert_true(!selisactive(), "RIS clears selection on erased screen");
    test_feed_string("\303\251");
    test_assert_cell(0, 0, "\303\251", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 1);
    test_print_ok("parser/hard_reset");
    return 0;
}
