#include "../common/test_common.h"
#include <string.h>

static void split_title(const char *seq, const char *expected) {
    size_t n = strlen(seq);
    for (size_t split = 0; split <= n; split++) {
        test_reset_terminal(3, 8);
        test_feed_bytes((const uint8_t *)seq, split);
        test_feed_bytes((const uint8_t *)seq + split, n - split);
        test_assert_true(!strcmp(test_last_title(), expected), "OSC title must survive every read boundary");
        test_assert_cursor(0, 0);
    }
}
int main(void) {
    split_title("\033]2;a\xc3\x9c" "b\007", "a\xc3\x9c" "b");
    split_title("\033]2;\xe2\x9c\x93\033\\", "\xe2\x9c\x93");
    const char *ignored[] = {
        "\033P\xc3\x9c" "payload\033\\X",
        "\033_\xe2\x9c\x93" "payload\033\\X",
        "\033^\xc3\x9c" "payload\033\\X"
    };
    for (unsigned int k = 0; k < sizeof ignored / sizeof *ignored; k++) {
        size_t n = strlen(ignored[k]);
        for (size_t split = 0; split <= n; split++) {
            test_reset_terminal(3, 8);
            test_feed_bytes((const uint8_t *)ignored[k], split);
            test_feed_bytes((const uint8_t *)ignored[k] + split, n - split);
            test_assert_cell(0, 0, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
            test_assert_cursor(0, 1);
        }
    }
    for (int cancel = 0x18; cancel <= 0x1a; cancel += 2) {
        test_reset_terminal(3, 8);
        test_feed_string("\033]2;keep\007\033]2;discard");
        uint8_t c = (uint8_t)cancel;
        test_feed_bytes(&c, 1);
        test_assert_true(!strcmp(test_last_title(), "keep"), "cancelled OSC must not run its command");
        test_feed_string("X");
        test_assert_cell(0, 0, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    }
    test_reset_terminal(3, 8);
    test_feed_string("\033]52;c;QQ==\030");
    test_assert_true(!term.osc52_pending, "cancelled clipboard command must not be queued");
    test_reset_terminal(3, 8);
    test_feed_string("\033%@\033]2;legacy\234");
    test_assert_true(!strcmp(test_last_title(), "legacy"), "legacy 8-bit ST must still terminate OSC");
    test_print_ok("parser/osc_boundaries");
    return 0;
}
