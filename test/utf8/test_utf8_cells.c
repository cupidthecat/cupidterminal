#include <stdint.h>

#include "../common/test_common.h"

int main(void) {
    static const char euro[] = "\xE2\x82\xAC";
    static const char emoji[] = "\xF0\x9F\x98\x80";

    test_reset_terminal(4, 16);
    test_feed_string("A\xE2\x82\xAC" "B");
    test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, euro, COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 2, "B", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_feed_string(emoji);
    test_assert_cell(0, 3, emoji, COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_print_ok("utf8/cells");
    return 0;
}
