#include "../common/test_common.h"

int main(void) {
    test_reset_terminal(4, 16);
    test_feed_string("\x1b[31;1mR\x1b[0mN");
    test_assert_cell(0, 0, "R", 1, COLOR_DEFAULT_BG, ATTR_BOLD);
    test_assert_cell(0, 1, "N", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_reset_terminal(4, 16);
    test_feed_string("\x1b[;31mA");
    test_assert_cell(0, 0, "A", 1, COLOR_DEFAULT_BG, 0);
    test_assert_attrs(1, COLOR_DEFAULT_BG, 0);

    test_feed_string("\x1b[3;4;7mX");
    test_assert_cell(0, 1, "X", 1, COLOR_DEFAULT_BG,
        (uint16_t)(ATTR_ITALIC | ATTR_UNDERLINE | ATTR_REVERSE));
    test_assert_attrs(1, COLOR_DEFAULT_BG,
        (uint16_t)(ATTR_ITALIC | ATTR_UNDERLINE | ATTR_REVERSE));

    test_feed_string("\x1b[1;2;44mB");
    test_assert_cell(0, 2, "B", 1, 4,
        (uint16_t)(ATTR_ITALIC | ATTR_UNDERLINE | ATTR_REVERSE | ATTR_BOLD | ATTR_FAINT));

    test_feed_string("\x1b[22mC");
    test_assert_cell(0, 3, "C", 1, 4,
        (uint16_t)(ATTR_ITALIC | ATTR_UNDERLINE | ATTR_REVERSE));

    test_feed_string("\x1b[24;27;39;49mD");
    test_assert_cell(0, 4, "D", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, ATTR_ITALIC);

    test_feed_string("\x1b[0m");
    test_assert_attrs(COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    /* 24-bit truecolor: 38;2;r;g;b and 48;2;r;g;b */
    test_reset_terminal(4, 16);
    test_feed_string("\x1b[38;2;255;0;128m\x1b[48;2;10;20;30mT");
    {
        uint32_t fg = 0x01000000u | (255u << 16) | (0u << 8) | 128u;
        uint32_t bg = 0x01000000u | (10u << 16) | (20u << 8) | 30u;
        test_assert_cell(0, 0, "T", fg, bg, 0);
    }

    /* Partial CSI across reads: 38;2;204;204;204m split */
    test_reset_terminal(4, 16);
    test_feed_string("\x1b[38;2;204;204;");  /* partial */
    test_feed_string("204mX");               /* rest + char */
    {
        uint32_t fg = 0x01000000u | (204u << 16) | (204u << 8) | 204u;
        test_assert_cell(0, 0, "X", fg, COLOR_DEFAULT_BG, 0);
    }

    test_print_ok("parser/sgr_attrs");
    return 0;
}
