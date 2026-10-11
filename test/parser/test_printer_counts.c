#define _XOPEN_SOURCE 700
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../common/test_common.h"

int main(void) {
    char path[] = "/tmp/cupid-printer-counts-XXXXXX";
    char output[32];
    int fd = mkstemp(path);
    test_assert_true(fd >= 0, "create printer fixture");
    close(fd);
    test_reset_terminal(2, 16);
    test_assert_true(tprinter_open(path) == 0, "open printer fixture");
    term.print_mode = 0;
    test_feed_string("\033[4294967301iHIDDEN");
    test_feed_string("\033[999999999999999999999999999999999iHIDDEN");
    test_assert_true(!term.print_mode, "overflow must not enable printing");
    test_feed_string("\033[5iVISIBLE\033[4iHIDDEN");
    tprinter_close();
    fd = open(path, O_RDONLY);
    test_assert_true(fd >= 0, "read printer fixture");
    ssize_t n = read(fd, output, sizeof output);
    close(fd);
    unlink(path);
    const char expected[] = "VISIBLE\033[4i";
    test_assert_true(n == (ssize_t)strlen(expected) &&
        !memcmp(output, expected, sizeof expected - 1),
        "only explicit printer enable may mirror bytes");
    test_print_ok("parser/printer_counts");
    return 0;
}
