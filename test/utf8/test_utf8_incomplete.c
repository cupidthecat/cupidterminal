#include <stdint.h>

#include "../common/test_common.h"

int main(void) {
    static const uint8_t part1[] = {0xE2, 0x82};
    static const uint8_t part2[] = {0xAC};
    static const uint8_t emoji_part1[] = {0xF0, 0x9F, 0x98};
    static const uint8_t emoji_part2[] = {0x80};
    static const char emoji[] = "\xF0\x9F\x98\x80";

    test_reset_terminal(2, 8);
    test_feed_bytes(part1, sizeof(part1));
    test_assert_cursor(0, 0);
    test_assert_cell(0, 0, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_feed_bytes(part2, sizeof(part2));
    test_assert_cell(0, 0, "\xE2\x82\xAC", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 1);

    test_feed_string("A");
    test_feed_bytes(emoji_part1, sizeof(emoji_part1));
    test_assert_cell(0, 1, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_feed_bytes(emoji_part2, sizeof(emoji_part2));
    test_assert_cell(0, 2, emoji, COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_true(term_state.col > 2, "cursor should advance after completed 4-byte UTF-8 sequence");

    test_print_ok("utf8/incomplete_sequences");
    return 0;
}
