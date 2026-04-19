/*
 * Wide glyphs (CJK, emoji): lead cell + trailing continuation cell.
 * Cursor must advance by 2. Phase 3.7: representation uses ATTR_WIDE on the
 * lead cell and ATTR_WDUMMY on the trailing continuation cell.
 *
 * Phase 3.6: reads the new model — glyph content via .u and term_render_cluster.
 */
#include "../common/test_common.h"
#include <string.h>

int main(void) {
    /* CJK ideograph: U+4E2D "中" (UTF-8: 0xE4 0xB8 0xAD), width 2 */
    test_reset_terminal(2, 6);
    test_feed_string("\xe4\xb8\xad");
    test_assert_cursor(0, 2);

    /* Base codepoint must be U+4E2D */
    test_assert_true(term_lines[0].line[0].u == (Rune)0x4E2D,
        "lead cell base codepoint must be U+4E2D (中)");

    /* Full cluster readback */
    {
        char cluster[64];
        size_t n = term_render_cluster(0, 0, cluster, sizeof cluster);
        test_assert_true(strcmp(cluster, "\xe4\xb8\xad") == 0,
            "lead cell holds wide glyph bytes");
        (void)n;
    }

    test_assert_true(term_lines[0].line[0].mode & ATTR_WIDE,
        "lead cell ATTR_WIDE");
    test_assert_true(term_lines[0].line[1].mode & ATTR_WDUMMY,
        "trailing cell ATTR_WDUMMY");

    /* Two wide glyphs in a row consume 4 cells */
    test_reset_terminal(2, 6);
    test_feed_string("\xe4\xb8\xad\xe6\x96\x87");  /* 中文 */
    test_assert_cursor(0, 4);
    test_assert_true(term_lines[0].line[2].mode & ATTR_WIDE,
        "second lead cell ATTR_WIDE");
    test_assert_true(term_lines[0].line[3].mode & ATTR_WDUMMY,
        "second trailing cell ATTR_WDUMMY");

    test_print_ok("screen/wide_glyphs");
    return 0;
}
