#include "../common/test_common.h"
#include <string.h>

int main(void) {
    const char *seqs[] = {"\033[2\nC", "\2332\nC"};
    for (size_t n = 0; n < sizeof seqs / sizeof *seqs; n++) {
        size_t len = strlen(seqs[n]);
        for (size_t split = 0; split <= len; split++) {
            test_reset_terminal(4, 16);
            test_feed_bytes((const uint8_t *)seqs[n], split);
            if (split >= len - 1)
                test_assert_true(term.row == 1, "LF executes before the CSI final byte arrives");
            test_feed_bytes((const uint8_t *)seqs[n] + split, len - split);
            test_assert_cursor(1, 2);
        }
    }
    test_reset_terminal(4, 16);
    test_feed_string("abc\033[2\rC");
    test_assert_cursor(0, 2);
    test_feed_string("\033[2\bC");
    test_assert_cursor(0, 3);
    test_feed_string("\033[2\tC");
    test_assert_cursor(0, 10);
    test_reset_terminal(3, 8);
    test_feed_string("\033)0\033[0\016mqq");
    test_assert_true(term.gl == 1, "SO inside CSI selects G1");
    test_assert_true(term_lines[0].line[0].u == 0x2500, "selected charset affects output");
    test_feed_string("\033[0\017mq");
    test_assert_true(term.gl == 0, "SI inside CSI selects G0");
    test_assert_cell(0, 2, "q", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    /* NUL and DEL do not split a numeric parameter or print a cell. */
    const uint8_t ignored[] = {27, '[', '1', 0, 127, '2', 'C'};
    test_reset_terminal(3, 16);
    for (size_t i = 0; i < sizeof ignored; i++) test_feed_bytes(ignored + i, 1);
    test_assert_cursor(0, 12);
    test_print_ok("parser/csi_embedded_controls");
    return 0;
}
