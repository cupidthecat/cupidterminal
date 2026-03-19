/*
 * Printable character handling tests (Phase 3 Tier 1).
 * Tests basic ASCII, space, and that control chars don't print.
 */
#include "../common/test_common.h"

int main(void) {
    /* Basic ASCII printables */
    test_reset_terminal(2, 16);
    test_feed_string("Hello");
    test_assert_cell(0, 0, "H", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 4, "o", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 5);

    /* Space is printable */
    test_reset_terminal(2, 16);
    test_feed_string("a b");
    test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, " ", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 2, "b", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* C0 controls (0x00-0x1F) do not print */
    test_reset_terminal(2, 16);
    test_feed_string("a");
    test_feed_bytes((const uint8_t *)"\x00\x01\x02", 3);
    test_feed_string("b");
    test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, "b", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cursor(0, 2);

    /* DEL (0x7F) does not print */
    {
        static const uint8_t del_seq[] = {'a', 0x7F, 'b'};
        test_reset_terminal(2, 16);
        test_feed_bytes(del_seq, sizeof(del_seq));
        test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
        test_assert_cell(0, 1, "b", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    }

    /* Invalid UTF-8 (0xC0 + 'b') is rejected; next valid char writes */
    test_reset_terminal(2, 16);
    test_feed_string("a");
    test_feed_bytes((const uint8_t *)"\xC0\x62", 2);  /* invalid 2-byte sequence */
    test_feed_string("b");
    test_assert_cell(0, 0, "a", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(0, 1, "b", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_print_ok("parser/printables");
    return 0;
}
