/*
 * SGR edge-case tests: bright colors, 256-color, combined attrs.
 */
#include "../common/test_common.h"

int main(void) {
    /* Bright FG (90-97) */
    test_reset_terminal(2, 16);
    test_feed_string("\x1b[91mR\x1b[0m");
    test_assert_cell(0, 0, "R", 9, COLOR_DEFAULT_BG, 0);  /* 91 -> index 9 */
    test_assert_attrs(COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* Bright BG (100-107) */
    test_reset_terminal(2, 16);
    test_feed_string("\x1b[101mX\x1b[0m");
    test_assert_cell(0, 0, "X", COLOR_DEFAULT_FG, 9, 0);  /* 101 -> index 9 */
    test_assert_attrs(COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* 256-color: ESC[38;5;N and ESC[48;5;N */
    test_reset_terminal(2, 16);
    test_feed_string("\x1b[38;5;42mG\x1b[0m");
    test_assert_cell(0, 0, "G", 42, COLOR_DEFAULT_BG, 0);
    test_assert_attrs(COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_reset_terminal(2, 16);
    test_feed_string("\x1b[48;5;200mB\x1b[0m");
    test_assert_cell(0, 0, "B", COLOR_DEFAULT_FG, 200, 0);
    test_assert_attrs(COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* Default FG/BG: 39 and 49 */
    test_reset_terminal(2, 16);
    test_feed_string("\x1b[31;44mX\x1b[39mY\x1b[49m");
    test_assert_cell(0, 0, "X", 1, 4, 0);
    test_assert_cell(0, 1, "Y", COLOR_DEFAULT_FG, 4, 0);
    test_assert_attrs(COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* Multiple SGR in one sequence */
    test_reset_terminal(2, 16);
    test_feed_string("\x1b[1;4;31mB\x1b[0m");
    test_assert_cell(0, 0, "B", 1, COLOR_DEFAULT_BG, (uint16_t)(ATTR_BOLD | ATTR_UNDERLINE));
    test_assert_attrs(COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_print_ok("parser/sgr_edges");
    return 0;
}
