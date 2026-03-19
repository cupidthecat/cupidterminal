/*
 * Deterministic parser tests: CSI parameter parsing edge cases.
 * Tests empty params, missing params, semicolon-separated, and default values.
 */
#include "../common/test_common.h"

int main(void) {
    /* CUP: ESC[H = default 1;1 = top-left */
    test_reset_terminal(4, 8);
    test_feed_string("abc\x1b[HX");
    test_assert_cell(0, 0, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 1);

    /* CUP: ESC[;H = row default 1, col default 1 */
    test_reset_terminal(4, 8);
    test_feed_string("abc\x1b[;HX");
    test_assert_cell(0, 0, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* CUP: ESC[2;H = row 2, col default 1 */
    test_reset_terminal(4, 8);
    test_feed_string("abc\x1b[2;HY");
    test_assert_cell(1, 0, "Y", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* CUP: ESC[;3H = row default 1, col 3 */
    test_reset_terminal(4, 8);
    test_feed_string("abc\x1b[;3HZ");
    test_assert_cell(0, 2, "Z", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* CUP: ESC[2;3H = row 2, col 3 */
    test_reset_terminal(4, 8);
    test_feed_string("\x1b[2;3H@");
    test_assert_cell(1, 2, "@", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 3);

    /* CUU/CUD/CUF/CUB: 0 params = default 1; start (1,3), up->(0,3), down->(1,3), right->(1,4), left->(1,3) */
    test_reset_terminal(4, 8);
    test_feed_string("\x1b[2;4H\x1b[A\x1b[B\x1b[C\x1b[D!");
    test_assert_cell(1, 3, "!", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 4);

    /* ED: 0 params = 0 (erase from cursor to end) */
    test_reset_terminal(2, 6);
    test_feed_string("abcdef\x1b[1;4H\x1b[J");
    test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 2, "c", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 3, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 5, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* EL: 0 params = 0 (erase from cursor to end of line) */
    test_reset_terminal(2, 6);
    test_feed_string("abcdef\x1b[1;4H\x1b[K");
    test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 3, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 5, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* SGR: ESC[m = no params = reset (equivalent to 0) */
    test_reset_terminal(2, 8);
    test_feed_string("\x1b[31;1mX\x1b[mY");
    test_assert_cell(0, 0, "X", 1, COLOR_DEFAULT_BG, ATTR_BOLD);
    test_assert_cell(0, 1, "Y", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* SGR: ESC[;31m = leading empty param = 0 */
    test_reset_terminal(2, 8);
    test_feed_string("\x1b[1m\x1b[;31mA");
    test_assert_cell(0, 0, "A", 1, COLOR_DEFAULT_BG, 0);

    test_print_ok("parser/csi_params");
    return 0;
}
