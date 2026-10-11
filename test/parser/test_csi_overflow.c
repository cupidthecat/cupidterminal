#include "../common/test_common.h"
#include <string.h>

int main(void) {
    uint8_t seq[1600];
    for (int eight_bit = 0; eight_bit < 2; eight_bit++) {
        size_t start = 0;
        if (eight_bit) seq[start++] = 0x9b;
        else { seq[start++] = 27; seq[start++] = '['; }
        memset(seq + start, '0', 1400);
        size_t len = start + 1400;
        seq[len++] = '2'; seq[len++] = 'J'; seq[len++] = 'Z';
        const size_t splits[] = {0, 1, 1000, 1024, 1100, 1400};
        for (size_t k = 0; k < sizeof splits / sizeof *splits; k++) {
            test_reset_terminal(3, 8);
            test_feed_string("abc");
            test_feed_bytes(seq, splits[k]);
            test_feed_bytes(seq + splits[k], len - splits[k]);
            test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
            test_assert_cell(0, 3, "Z", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
            test_assert_cursor(0, 4);
        }
    }
    /* Exercise the discard state over many reads, then cancellation and reset. */
    const char *recovery[] = {"\030R", "\032R", "\033cR"};
    for (size_t k = 0; k < sizeof recovery / sizeof *recovery; k++) {
        test_reset_terminal(4, 8);
        seq[0] = 27; seq[1] = '['; memset(seq + 2, '0', 1400);
        for (size_t i = 0; i < 1402; i++) test_feed_bytes(seq + i, 1);
        test_feed_string(recovery[k]);
        test_assert_cell(0, 0, "R", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
        test_assert_true(!term.csi_discard, "cancellation clears oversized CSI state");
    }
    test_reset_terminal(4, 8);
    seq[0] = 27; seq[1] = '['; memset(seq + 2, '0', 1400);
    test_feed_bytes(seq, 1402);
    test_feed_string("\n\033[2;3HQ");
    test_assert_cell(1, 2, "Q", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(1, 3);
    test_print_ok("parser/csi_overflow");
    return 0;
}
