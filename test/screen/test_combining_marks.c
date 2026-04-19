/*
 * Combining marks: base + combining codepoint occupy ONE visual cell.
 * Locks current cluster-per-cell behavior so Phase 3 (Rune + side-channel)
 * cannot regress it.
 *
 * Phase 3.6: reads the new model — base via .u, combining marks via ATTR_HASCOMB
 * + TermLine.combs, full cluster readback via term_render_cluster.
 */
#include "../common/test_common.h"
#include <string.h>

int main(void) {
    /* "e" + U+0301 (COMBINING ACUTE ACCENT, UTF-8: 0xCC 0x81) = "é" */
    test_reset_terminal(2, 5);
    test_feed_string("e\xcc\x81");
    test_assert_cursor(0, 1);

    /* Base codepoint must be 'e' */
    test_assert_true(term_lines[0].line[0].u == (Rune)'e',
        "cell[0,0] base codepoint must be 'e'");
    /* ATTR_HASCOMB must be set */
    test_assert_true((term_lines[0].line[0].mode & ATTR_HASCOMB) != 0,
        "cell[0,0] must have ATTR_HASCOMB set");
    /* The combining mark must be U+0301 */
    test_assert_true(term_lines[0].combs != NULL && term_lines[0].ncombs > 0,
        "row 0 must have combining mark side-channel entry");
    test_assert_true(term_lines[0].combs[0].col == 0,
        "combining mark must reference column 0");
    test_assert_true(term_lines[0].combs[0].n >= 1,
        "combining mark entry must have at least one rune");
    test_assert_true(term_lines[0].combs[0].runes[0] == (Rune)0x0301,
        "combining mark rune must be U+0301");

    /* Full cluster readback via term_render_cluster */
    {
        char cluster[64];
        size_t n = term_render_cluster(0, 0, cluster, sizeof cluster);
        test_assert_true(strcmp(cluster, "e\xcc\x81") == 0,
            "cell[0,0] must contain base+combining cluster");
        (void)n;
    }

    /* Cell 1 must be empty */
    {
        char cluster[64];
        size_t n = term_render_cluster(0, 1, cluster, sizeof cluster);
        test_assert_true(n == 0 && cluster[0] == '\0',
            "cell[0,1] must be empty");
    }

    /* Two combining marks chained onto same base */
    test_reset_terminal(2, 5);
    test_feed_string("a\xcc\x81\xcc\x80");  /* a + acute + grave */
    test_assert_cursor(0, 1);

    /* Base must be 'a' */
    test_assert_true(term_lines[0].line[0].u == (Rune)'a',
        "cell[0,0] base must be 'a' for double-combining test");
    /* ATTR_HASCOMB must be set */
    test_assert_true((term_lines[0].line[0].mode & ATTR_HASCOMB) != 0,
        "cell[0,0] must have ATTR_HASCOMB for double-combining test");

    /* Full cluster must be base + two combining marks (length >= 3 bytes) */
    {
        char cluster[64];
        size_t n = term_render_cluster(0, 0, cluster, sizeof cluster);
        test_assert_true(n >= 3,
            "cell[0,0] must hold base + multiple combining marks");
    }

    test_print_ok("screen/combining_marks");
    return 0;
}
