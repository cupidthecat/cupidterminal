#include "../common/test_common.h"
#include <string.h>

int main(void) {
    const char *sequences[] = {"\033[2\030J", "\033[2\032J", "\2332\030J", "\2332\032J"};
    for (unsigned int k = 0; k < sizeof sequences / sizeof *sequences; k++) {
        size_t n = strlen(sequences[k]);
        for (size_t split = 0; split <= n; split++) {
            test_reset_terminal(3, 8);
            test_feed_string("abc");
            test_feed_bytes((const uint8_t *)sequences[k], split);
            test_feed_bytes((const uint8_t *)sequences[k] + split, n - split);
            test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
            test_assert_cell(0, 3, "J", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
            test_assert_cursor(0, 4);
        }
    }
    const char *restart[] = {"\033[2\033[3;4H", "\2332\033[3;4H"};
    for (unsigned int k = 0; k < sizeof restart / sizeof *restart; k++) {
        for (size_t split = 0; split <= strlen(restart[k]); split++) {
            test_reset_terminal(3, 8);
            test_feed_bytes((const uint8_t *)restart[k], split);
            test_feed_bytes((const uint8_t *)restart[k] + split, strlen(restart[k]) - split);
            test_assert_cursor(2, 3);
        }
    }
    test_print_ok("parser/csi_cancellation");
    return 0;
}
