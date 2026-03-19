/*
 * Right-margin pending-wrap correctness tests (Phase 3 Tier 1).
 *
 * When a printable is written in the rightmost column, cursor enters
 * "pending wrap" state (col == term_cols). The next action determines
 * whether wrap occurs or is cancelled.
 */
#include "../common/test_common.h"

int main(void) {
    /* Printable at right margin -> pending wrap; next printable wraps */
    test_reset_terminal(2, 5);
    test_feed_string("ABCDE");
    test_assert_cursor(0, 5);  /* col=5 = term_cols, pending wrap */
    test_feed_string("F");
    test_assert_cell(0, 4, "E", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 0, "F", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 1);

    /* BS at pending wrap: move to last visible column, no wrap */
    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\bX");
    test_assert_cell(0, 4, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 5);

    /* LF at pending wrap: cancel wrap, newline, then cursor at term_cols-1 */
    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\nX");
    test_assert_cell(0, 4, "E", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 4, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 5);

    /* CR at pending wrap: col=0, no wrap */
    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\rZ");
    test_assert_cell(0, 0, "Z", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 4, "E", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 1);

    /* CUB at pending wrap: first step moves to term_cols-1 */
    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\x1b[DZ");
    test_assert_cell(0, 4, "Z", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 5);

    /* CUB twice at pending wrap: col = term_cols - 2 */
    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\x1b[2D");
    test_assert_cursor(0, 3);
    test_feed_string("Y");
    test_assert_cell(0, 3, "Y", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 4);

    /* CUF at pending wrap: cancel to col=term_cols-1, CUF+1 -> col=term_cols, X wraps */
    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\x1b[CX");
    test_assert_cell(1, 0, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 1);

    /* CUU at pending wrap: cancel, move up */
    test_reset_terminal(2, 5);
    test_feed_string("abcde\x1b[A@");
    test_assert_cell(0, 4, "@", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 5);

    /* CUD at pending wrap: cancel, move down */
    test_reset_terminal(3, 5);
    test_feed_string("ABCDE\x1b[B@");
    test_assert_cell(1, 4, "@", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 5);

    /* CUP/HVP at pending wrap: absolute position, no wrap state */
    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\x1b[2;1H@");
    test_assert_cell(1, 0, "@", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 1);

    /* ED at pending wrap: cancel before erase */
    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\x1b[J");
    test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 3, "D", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 4, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 4);

    /* EL at pending wrap: cancel before erase */
    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\x1b[K");
    test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 3, "D", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 4, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 4);

    /* HT at pending wrap: move to term_cols-1 */
    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\tX");
    test_assert_cell(0, 4, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 5);

    /* BEL at pending wrap: no cursor change, bell only */
    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\a");
    test_assert_cursor(0, 5);
    test_assert_mode("bell_rung", term_state.bell_rung, 1);

    test_print_ok("screen/pending_wrap");
    return 0;
}
