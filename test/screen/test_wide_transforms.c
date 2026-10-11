#include "../common/test_common.h"
#include <stdio.h>

static void assert_row_pairs(int row) {
    const Glyph *cells = term_lines[row].line;
    for (int c = 0; c < term_cols; c++) {
        if (cells[c].mode & ATTR_WIDE)
            test_assert_true(c + 1 < term_cols && (cells[c + 1].mode & ATTR_WDUMMY),
                             "wide lead must retain its dummy cell");
        if (cells[c].mode & ATTR_WDUMMY)
            test_assert_true(c > 0 && (cells[c - 1].mode & ATTR_WIDE),
                             "dummy cell must retain its wide lead");
    }
}

int main(void) {
    const char *edits[] = {"\033[@", "\033[P", "\033[4hX\033[4l"};
    for (unsigned int e = 0; e < sizeof edits / sizeof *edits; e++) {
        for (int col = 1; col <= 8; col++) {
            char position[32];
            test_reset_terminal(3, 8);
            test_feed_string("A\xe7\x95\x8c\xcc\x81" "BC\xe7\x95\x8c\xcc\x82");
            snprintf(position, sizeof position, "\033[1;%dH", col);
            test_feed_string(position);
            test_feed_string(edits[e]);
            assert_row_pairs(0);
        }
    }
    test_reset_terminal(3, 8);
    test_feed_string("A\xe7\x95\x8c" "B\033[1;3H\033[P");
    test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 2, "B", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_reset_terminal(3, 8);
    test_feed_string("ABC\xe7\x95\x8c\xcc\x81");
    tresize(3, 4);
    assert_row_pairs(0);
    test_assert_cell(0, 3, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_true(term_lines[0].ncombs == 0, "cropped wide base must lose its combining marks");
    tresize(3, 8);
    assert_row_pairs(0);
    test_feed_string("\033[?1049hABC\xe7\x95\x8c");
    tresize(3, 4);
    assert_row_pairs(0);
    test_feed_string("\033[?1049l");
    assert_row_pairs(0);
    test_print_ok("screen/wide_transforms");
    return 0;
}
