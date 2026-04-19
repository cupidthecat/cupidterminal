#include "../common/test_common.h"

int main(void) {
    test_reset_terminal(3, 4);

    test_feed_string("AAAA\r\nBBBB\r\nCCCC\r\nDDDD");

    test_assert_cell(0, 0, "B", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 0, "C", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(2, 0, "D", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    kscrollup_n(1);
    test_assert_mode("scrollback_offset", tgetscrolloffset(), 1);

    {
        const Glyph *r0 = tgetline(0);
        const Glyph *r1 = tgetline(1);
        const Glyph *r2 = tgetline(2);
        test_assert_true(r0 != NULL && r1 != NULL && r2 != NULL, "visible rows must exist");
        test_assert_true(r0[0].u == (Rune)'A', "row0 should come from history");
        test_assert_true(r1[0].u == (Rune)'B', "row1 should map to live row0");
        test_assert_true(r2[0].u == (Rune)'C', "row2 should map to live row1");
    }

    kscrolldown_n(1);
    test_assert_mode("scrollback_offset", tgetscrolloffset(), 0);

    test_print_ok("screen/scrollback_history");
    return 0;
}
