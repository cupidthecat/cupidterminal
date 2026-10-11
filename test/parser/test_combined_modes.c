#include "../common/test_common.h"

int main(void) {
    test_reset_terminal(4, 8);
    test_feed_string("\033[2;4;12;20h");
    test_assert_true(term.keyboard_lock && term.insert_mode && !term.echo_mode && term.lnm_mode,
                     "SM must set every recognized mode alongside keyboard lock");
    test_feed_string("\033[2;4;12;20l");
    test_assert_true(!term.keyboard_lock && !term.insert_mode && term.echo_mode && !term.lnm_mode,
                     "RM must reset every recognized mode alongside keyboard lock");
    test_feed_string("\033[999;20;2;4h");
    test_assert_true(term.keyboard_lock && term.insert_mode && term.lnm_mode,
                     "unknown modes must not hide recognized modes");
    test_print_ok("parser/combined_modes");
    return 0;
}
