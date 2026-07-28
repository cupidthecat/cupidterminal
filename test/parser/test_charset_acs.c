/*
 * Charset and ACS mode behavior regression tests.
 * Ensures the four st-compatible ISO-2022 charset slots switch cleanly.
 */
#include "../common/test_common.h"

int main(void) {
    /* SO/SI should toggle G1/G0 and make ACS drawing deterministic. */
    test_reset_terminal(2, 8);
    test_feed_string("\x1b)0");      /* designate G1 as DEC Special Graphics */
    test_feed_bytes((const uint8_t *)"\x0Eq", 2);  /* SO + 'q' => horizontal line */
    test_assert_cell(0, 0, "\xe2\x94\x80", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_feed_bytes((const uint8_t *)"\x0Fo", 2);  /* SI + 'o' => normal ASCII */
    test_assert_cell(0, 1, "o", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_mode("gl_after_si", term.gl, 0);

    /* ESC n/o select G2/G3; undesignated slots remain ASCII. */
    test_reset_terminal(2, 8);
    test_feed_string("\x1b)0\x1bn");
    test_feed_string("o");
    test_assert_cell(0, 0, "o", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_mode("gl_after_esc_n", term.gl, 2);

    test_reset_terminal(2, 8);
    test_feed_string("\x1b)0\x1bo");
    test_feed_string("q");
    test_assert_cell(0, 0, "q", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_mode("gl_after_esc_o", term.gl, 3);

    test_print_ok("parser/charset_acs");
    return 0;
}
