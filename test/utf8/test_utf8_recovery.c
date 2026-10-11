#include "../common/test_common.h"
#include <string.h>

int main(void) {
    const char *bad[] = {"\xc3" "A", "\xe2\x82" "A", "\xf0\x9f\x98" "A"};
    for (unsigned int k = 0; k < sizeof bad / sizeof *bad; k++) {
        size_t n = strlen(bad[k]);
        for (size_t split = 0; split <= n; split++) {
            test_reset_terminal(3, 8);
            test_feed_bytes((const uint8_t *)bad[k], split);
            test_feed_bytes((const uint8_t *)bad[k] + split, n - split);
            test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
        }
    }
    test_reset_terminal(3, 8);
    test_feed_string("\xc3\xc3\xa9");
    test_assert_cell(0, 0, "\xc3\xa9", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_print_ok("utf8/recovery");
    return 0;
}
