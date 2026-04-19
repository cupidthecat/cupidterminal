/*
 * Regression-lock: combining marks must survive scroll, insert-line, delete-line.
 * Phase 5 Critical #1 fix — pre-fix, combining marks were silently dropped after
 * the row carrying them moved to a different row slot.
 */
#include "../common/test_common.h"
#include <string.h>

int main(void) {
    char cluster[64];
    size_t n;

    /* ------------------------------------------------------------------ */
    /* (1) Scroll up: write "é" on row 1, fill 3 lines so a scroll happens,
           then assert "é" still renders correctly on row 0 (post-scroll). */
    /* ------------------------------------------------------------------ */
    test_reset_terminal(3, 5);
    /* Row 0: empty (just CR+LF moves to row 1) */
    /* Row 1: é (e + combining acute U+0301) */
    /* Row 2: XXX */
    test_feed_string("\r\ne\xcc\x81\r\nXXX");
    /* Force a scroll: newline from row 2 pushes everything up by one */
    test_feed_string("\r\nYYY");
    /* After scroll: old row 1 (é) is now at row 0 */
    n = term_render_cluster(0, 0, cluster, sizeof cluster);
    test_assert_true(n >= 3 && strcmp(cluster, "e\xcc\x81") == 0,
        "combining mark must survive scroll-up");

    /* ------------------------------------------------------------------ */
    /* (2) Insert line (CSI L): place "é" on row 1, then CSI L to insert a
           blank line before it (pushing it to row 2). The mark must still
           render. */
    /* ------------------------------------------------------------------ */
    test_reset_terminal(4, 5);
    test_feed_string("\r\ne\xcc\x81");           /* cursor at row 1; é at (1,0) */
    /* Go to row 0 col 0, insert 1 blank line → é shifts to row 2 */
    test_feed_string("\x1b[1;1H\x1b[L");
    n = term_render_cluster(2, 0, cluster, sizeof cluster);
    test_assert_true(n >= 3 && strcmp(cluster, "e\xcc\x81") == 0,
        "combining mark must survive CSI L (insert line)");

    /* ------------------------------------------------------------------ */
    /* (3) Delete line (CSI M): place "é" on row 2, delete row 0, mark moves
           to row 1. */
    /* ------------------------------------------------------------------ */
    test_reset_terminal(4, 5);
    test_feed_string("\r\n\r\ne\xcc\x81");       /* é at (2,0) */
    /* Go to row 0, delete 1 line → é moves from row 2 to row 1 */
    test_feed_string("\x1b[1;1H\x1b[M");
    n = term_render_cluster(1, 0, cluster, sizeof cluster);
    test_assert_true(n >= 3 && strcmp(cluster, "e\xcc\x81") == 0,
        "combining mark must survive CSI M (delete line)");

    test_print_ok("screen/scroll_combining");
    return 0;
}
