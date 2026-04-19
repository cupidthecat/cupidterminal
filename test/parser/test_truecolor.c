/*
 * Truecolor SGR: 38;2;r;g;b sets fg to packed 24-bit RGB; 48;2;r;g;b sets bg.
 * Uses current COLOR_TRUE_RGB_BASE encoding; Phase 3 must preserve the
 * same packed value semantics under TRUECOLOR()/IS_TRUECOL().
 */
#include "../common/test_common.h"

int main(void) {
    /* Foreground RGB(204,102,51) -> packed 0x01CC6633 */
    test_reset_terminal(1, 5);
    test_feed_string("\x1b[38;2;204;102;51m");
    test_assert_attrs(0x01CC6633u, COLOR_DEFAULT_BG, 0);

    /* Background RGB(0,128,255) -> packed 0x010080FF */
    test_reset_terminal(1, 5);
    test_feed_string("\x1b[48;2;0;128;255m");
    test_assert_attrs(COLOR_DEFAULT_FG, 0x010080FFu, 0);

    /* Glyph carries the packed colors */
    test_reset_terminal(1, 5);
    test_feed_string("\x1b[38;2;255;0;0mX");
    test_assert_cell(0, 0, "X", 0x01FF0000u, COLOR_DEFAULT_BG, 0);

    test_print_ok("parser/truecolor");
    return 0;
}
