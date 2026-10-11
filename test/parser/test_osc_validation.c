#include <string.h>
#include "../common/test_common.h"

int main(void) {
    const char *commands[] = {
        "\033]garbage;changed\a", "\033]2junk;changed\a",
        "\033]4294967298;changed\a", "\033]-4294967294;changed\a",
        "\033]999999999999999999999999999999999;changed\a"
    };
    for (unsigned i = 0; i < sizeof commands / sizeof *commands; i++) {
        test_reset_terminal(2, 8);
        test_feed_string("\033]2;keep\a");
        test_feed_string(commands[i]);
        test_assert_true(!strcmp(test_last_title(), "keep"),
            "malformed OSC numbers must not alias a title command");
    }
    test_reset_terminal(2, 8);
    test_feed_string("\033]4;4294967297;#010203\a");
    test_assert_true(!term.palette_overridden[1],
        "overflowed palette indices must not wrap into valid entries");
    test_feed_string("\033]4;1;#010203\a");
    test_assert_true(term.palette_overridden[1], "valid palette index must work");

    const char *bad[] = {"====", "A===", "=AAA", "QQ=", "QQ==AA", "QQ===", "Q"};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        test_reset_terminal(2, 8);
        test_feed_string("\033]52;c;");
        test_feed_string(bad[i]);
        test_feed_string("\a");
        test_assert_true(!term.osc52_pending,
            "malformed base64 must not queue a clipboard update");
    }
    const char *good[] = {"QQ==", "QQ", "QUI=", "QUI", "QUJD", " Q Q = = \n"};
    const char *decoded[] = {"A", "A", "AB", "AB", "ABC", "A"};
    for (unsigned i = 0; i < sizeof good / sizeof *good; i++) {
        test_reset_terminal(2, 8);
        test_feed_string("\033]52;c;");
        test_feed_string(good[i]);
        test_feed_string("\a");
        test_assert_true(term.osc52_pending && term.osc52_len == strlen(decoded[i]) &&
            !memcmp(term.osc52_buf, decoded[i], term.osc52_len),
            "valid padded and unpadded clipboard data must still decode");
    }
    test_print_ok("parser/osc_validation");
    return 0;
}
