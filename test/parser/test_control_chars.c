#include "../common/test_common.h"
#include <string.h>

int main(void) {
    test_reset_terminal(4, 16);
    test_feed_string("abc\rZ");
    test_assert_cell(0, 0, "Z", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, "b", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 2, "c", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_reset_terminal(4, 16);
    test_feed_string("a\tb");
    test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 8, "b", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_reset_terminal(4, 16);
    test_feed_string("abc\bX");
    test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, "b", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 2, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* BEL is processed immediately via xbell() — no bell_rung flag remains */
    test_feed_string("\a");

    test_reset_terminal(3, 8);
    test_feed_string("A\nB");
    test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 1, "B", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 2);

    test_reset_terminal(2, 8);
    test_feed_string("\bX");
    test_assert_cell(0, 0, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 1);

    test_reset_terminal(2, 5);
    test_feed_string("ABCDE\tX");
    test_assert_cell(0, 4, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 5);

    /* C1 RI (0x8D): reverse index; at top margin it scrolls down one line. */
    test_reset_terminal(3, 4);
    test_feed_string("AAAA\r\nBBBB\r\nCCCC");
    test_feed_string("\x1b[H");
    {
        const uint8_t ri[] = {0x8D};
        test_feed_bytes(ri, sizeof(ri));
    }
    test_assert_cell(0, 0, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(1, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(2, 0, "B", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 0);

    /* C1 CSI (0x9B) should behave like ESC [ */
    test_reset_terminal(3, 6);
    {
        const uint8_t seq[] = {0x9B, '2', ';', '3', 'H', '@'};
        test_feed_bytes(seq, sizeof(seq));
    }
    test_assert_cell(1, 2, "@", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 3);

    /* C1 OSC (0x9D) + ST (0x9C) */
    test_reset_terminal(2, 4);
    {
        const uint8_t seq[] = {0x9D, '2', ';', 'h', 'i', 0x9C};
        test_feed_bytes(seq, sizeof(seq));
    }
    test_assert_true(strcmp(term.window_title, "hi") == 0, "C1 OSC title not applied");
    test_assert_mode("title_dirty", term.title_dirty, 1);

    /* C1 DCS (0x90) is ignored until ST, then regular text resumes */
    test_reset_terminal(2, 4);
    {
        const uint8_t seq[] = {0x90, 'x', 'y', 'z', 0x9C, 'A'};
        test_feed_bytes(seq, sizeof(seq));
    }
    test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 1);

    test_print_ok("parser/control_chars");
    return 0;
}
