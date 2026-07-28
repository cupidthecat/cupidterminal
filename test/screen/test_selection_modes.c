/*
 * Regression-lock test: selection state across four modes.
 *
 * The test links the active X11-free selection model in cupid.c. It directly
 * sets term.sel_* fields and asserts that:
 *   1. The fields can be written and read back without crashing.
 *   2. The encoded values match the documented semantics:
 *        sel_type 0 = SEL_REGULAR, 1 = SEL_RECTANGULAR
 *        sel_snap 0 = none, 1 = SNAP_WORD, 2 = SNAP_LINE
 *   3. sel_active == 1 after setup (selection is live).
 *   4. The anchor / drag-end coordinates round-trip correctly.
 *   5. Resetting sel_active to 0 clears the selection without
 *      corrupting the rest of Term.
 *   6. The term_lines cells that would be covered by each
 *      selection contain the expected characters (verifying that
 *      test_feed_string placed them correctly).
 */

#include "../common/test_common.h"

/* SNAP_NONE is the only selection constant not exported by cupid.h. */
#ifndef SEL_REGULAR
#define SEL_REGULAR    0
#define SEL_RECTANGULAR 1
#endif
#define SNAP_NONE      0
#ifndef SNAP_WORD
#define SNAP_WORD      1
#define SNAP_LINE      2
#endif

int main(void) {

    /* ------------------------------------------------------------------ */
    /* Common setup: 3 rows x 12 cols, two lines with space-separated words */
    /* Row 0: "ABCDE FGHIJ "  (trailing space fills to col 11)             */
    /* Row 1: "KLMNO PQRST "                                               */
    /* Row 2: (empty / cursor row after the two \n)                        */
    /* ------------------------------------------------------------------ */

    test_reset_terminal(3, 12);
    /* Use \r\n so each line feeds and returns to column 0 correctly.       */
    /* A bare \n moves the cursor down without resetting the column.        */
    test_feed_string("ABCDE FGHIJ\r\nKLMNO PQRST\r\n");

    /* Verify the buffer has the expected characters before selection tests */
    test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 4, "E", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 5, " ", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 6, "F", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 10, "J", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 0, "K", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 5, " ", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 10, "T", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* ------------------------------------------------------------------ */
    /* MODE 1: SEL_REGULAR, SNAP_NONE                                      */
    /* Select from (row0,col0) to (row0,col4) — i.e., "ABCDE"             */
    /* ------------------------------------------------------------------ */
    term.sel_active     = 1;
    term.sel_type       = SEL_REGULAR;
    term.sel_snap       = SNAP_NONE;
    term.sel_anchor_row = 0;
    term.sel_anchor_col = 0;
    term.sel_row        = 0;
    term.sel_col        = 4;

    test_assert_true(term.sel_active == 1,
        "regular/no-snap: sel_active should be 1");
    test_assert_true(term.sel_type == SEL_REGULAR,
        "regular/no-snap: sel_type should be SEL_REGULAR (0)");
    test_assert_true(term.sel_snap == SNAP_NONE,
        "regular/no-snap: sel_snap should be SNAP_NONE (0)");
    test_assert_true(term.sel_anchor_row == 0,
        "regular/no-snap: anchor row should be 0");
    test_assert_true(term.sel_anchor_col == 0,
        "regular/no-snap: anchor col should be 0");
    test_assert_true(term.sel_row == 0,
        "regular/no-snap: drag-end row should be 0");
    test_assert_true(term.sel_col == 4,
        "regular/no-snap: drag-end col should be 4");

    /* The cells covered by this selection must contain A..E */
    test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, "B", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 2, "C", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 3, "D", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 4, "E", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* ------------------------------------------------------------------ */
    /* MODE 2: SEL_RECTANGULAR, SNAP_NONE                                  */
    /* Select column band 6..10 on rows 0..1 — "FGHIJ" / "PQRST"         */
    /* ------------------------------------------------------------------ */
    term.sel_active     = 1;
    term.sel_type       = SEL_RECTANGULAR;
    term.sel_snap       = SNAP_NONE;
    term.sel_anchor_row = 0;
    term.sel_anchor_col = 6;
    term.sel_row        = 1;
    term.sel_col        = 10;

    test_assert_true(term.sel_active == 1,
        "rectangular: sel_active should be 1");
    test_assert_true(term.sel_type == SEL_RECTANGULAR,
        "rectangular: sel_type should be SEL_RECTANGULAR (1)");
    test_assert_true(term.sel_snap == SNAP_NONE,
        "rectangular: sel_snap should be SNAP_NONE (0)");
    test_assert_true(term.sel_anchor_row == 0,
        "rectangular: anchor row should be 0");
    test_assert_true(term.sel_anchor_col == 6,
        "rectangular: anchor col should be 6");
    test_assert_true(term.sel_row == 1,
        "rectangular: drag-end row should be 1");
    test_assert_true(term.sel_col == 10,
        "rectangular: drag-end col should be 10");

    /* Column band 6..10 in row 0 contains F..J */
    test_assert_cell(0, 6,  "F", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 10, "J", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    /* Column band 6..10 in row 1 contains P..T */
    test_assert_cell(1, 6,  "P", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 10, "T", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* Columns outside the band on those rows are NOT in the rectangle */
    test_assert_cell(0, 5, " ", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 5, " ", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* ------------------------------------------------------------------ */
    /* MODE 3: SEL_REGULAR, SNAP_WORD                                      */
    /* Anchor inside the first word of row 0: col 2 ("C").                 */
    /* Drag-end inside the second word of row 0: col 8 ("I").              */
    /* With word-snap the effective start expands left to col 0 ("A") and  */
    /* the effective end expands right to col 10 ("J") — but that          */
    /* expansion is done at readback time by selection_get_effective_bounds */
    /* (in input.c, not linked here).  We assert the raw stored fields.    */
    /* ------------------------------------------------------------------ */
    term.sel_active     = 1;
    term.sel_type       = SEL_REGULAR;
    term.sel_snap       = SNAP_WORD;
    term.sel_anchor_row = 0;
    term.sel_anchor_col = 2;
    term.sel_row        = 0;
    term.sel_col        = 8;

    test_assert_true(term.sel_active == 1,
        "word-snap: sel_active should be 1");
    test_assert_true(term.sel_type == SEL_REGULAR,
        "word-snap: sel_type should be SEL_REGULAR (0)");
    test_assert_true(term.sel_snap == SNAP_WORD,
        "word-snap: sel_snap should be SNAP_WORD (1)");
    test_assert_true(term.sel_anchor_row == 0,
        "word-snap: anchor row should be 0");
    test_assert_true(term.sel_anchor_col == 2,
        "word-snap: anchor col should be 2");
    test_assert_true(term.sel_row == 0,
        "word-snap: drag-end row should be 0");
    test_assert_true(term.sel_col == 8,
        "word-snap: drag-end col should be 8");

    /* The buffer cells that word-snap would expand to cover: A..J (cols 0..10) */
    test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 10, "J", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* ------------------------------------------------------------------ */
    /* MODE 4: SEL_REGULAR, SNAP_LINE                                      */
    /* Anchor at (row0, col3), drag-end at (row1, col7).                   */
    /* With line-snap the effective bounds extend to (row0,col0)..(row1,   */
    /* col11) — again computed at readback time in input.c.                */
    /* We assert the raw stored fields and the content of the full rows.    */
    /* ------------------------------------------------------------------ */
    term.sel_active     = 1;
    term.sel_type       = SEL_REGULAR;
    term.sel_snap       = SNAP_LINE;
    term.sel_anchor_row = 0;
    term.sel_anchor_col = 3;
    term.sel_row        = 1;
    term.sel_col        = 7;

    test_assert_true(term.sel_active == 1,
        "line-snap: sel_active should be 1");
    test_assert_true(term.sel_type == SEL_REGULAR,
        "line-snap: sel_type should be SEL_REGULAR (0)");
    test_assert_true(term.sel_snap == SNAP_LINE,
        "line-snap: sel_snap should be SNAP_LINE (2)");
    test_assert_true(term.sel_anchor_row == 0,
        "line-snap: anchor row should be 0");
    test_assert_true(term.sel_anchor_col == 3,
        "line-snap: anchor col should be 3");
    test_assert_true(term.sel_row == 1,
        "line-snap: drag-end row should be 1");
    test_assert_true(term.sel_col == 7,
        "line-snap: drag-end col should be 7");

    /* Verify the full rows that line-snap would cover */
    /* Row 0: A B C D E   F G H I J   (cols 0..10, col 11 is space) */
    test_assert_cell(0, 0,  "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 5,  " ", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 10, "J", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    /* Row 1: K L M N O   P Q R S T   (cols 0..10, col 11 is space) */
    test_assert_cell(1, 0,  "K", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 5,  " ", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 10, "T", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* ------------------------------------------------------------------ */
    /* Clear selection and verify terminal state is not corrupted           */
    /* ------------------------------------------------------------------ */
    term.sel_active = 0;
    test_assert_true(term.sel_active == 0,
        "after clear: sel_active should be 0");
    /* The rest of terminal state should be undisturbed */
    test_assert_true(term.row == 2,
        "after clear: cursor row should still be 2 (after two \\r\\n)");
    test_assert_true(term.col == 0,
        "after clear: cursor col should still be 0 (after \\r\\n)");
    /* Buffer cells should be intact */
    test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 0, "K", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_print_ok("screen/selection_modes");
    return 0;
}
