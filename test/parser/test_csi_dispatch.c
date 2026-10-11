#include "../common/test_common.h"
#include <string.h>

int main(void) {
    const char *ignored[] = {
        "\033[2$J", "\033[>4;2m", "\033[=31m", "\033[<31m",
        "\033[1?2J", "\033[2 J", "\033[1:2H", "\033[2$3J",
        "\2332$J", "\233>4;2m"
    };
    for (size_t n = 0; n < sizeof ignored / sizeof *ignored; n++) {
        size_t len = strlen(ignored[n]);
        for (size_t split = 0; split <= len; split++) {
            test_reset_terminal(3, 8);
            test_feed_string("abc");
            test_feed_bytes((const uint8_t *)ignored[n], split);
            test_feed_bytes((const uint8_t *)ignored[n] + split, len - split);
            test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
            test_assert_cursor(0, 3);
            test_assert_attrs(COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
        }
    }
    test_reset_terminal(3, 8);
    test_feed_string("\033[5 q");
    test_assert_true(term.cursorshape == 5, "DECSCUSR space intermediate remains supported");
    test_feed_string("\033[?25l");
    test_assert_true(!term.cursor_visible, "DEC private modes remain supported");
    test_feed_string("\033[38:2:1:2:3m");
    test_assert_attrs(TRUECOLOR(1, 2, 3), COLOR_DEFAULT_BG, 0);
    test_feed_string("\033[2;4H");
    test_assert_cursor(1, 3);
    test_feed_string("\033[2J");
    test_assert_true(term_lines[0].line[0].u == 0, "ordinary erase still works");
    test_print_ok("parser/csi_dispatch");
    return 0;
}
