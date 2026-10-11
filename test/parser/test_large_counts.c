#include "../common/test_common.h"
#include <limits.h>
#include <stdio.h>
#include <unistd.h>

static void move(const char *suffix, int row, int col) {
    char seq[64];
    test_reset_terminal(4, 8);
    test_feed_string("\033[2;2H");
    snprintf(seq, sizeof seq, "\033[%d%s", INT_MAX, suffix);
    test_feed_string(seq);
    test_assert_cursor(row, col);
}

int main(void) {
    alarm(5); /* Large tab counts must finish without billions of iterations. */
    move("B", 3, 1);
    move("E", 3, 0);
    move("C", 1, 7);
    move("a", 1, 7);
    move("A", 0, 1);
    move("F", 0, 0);
    move("D", 1, 0);
    move("I", 1, 7);
    move("Z", 1, 0);
    test_reset_terminal(4, 8);
    test_feed_string("\033[2;4r\033[?6h\033[2147483647;2H");
    test_assert_cursor(3, 1);
    test_feed_string("\033[2147483647d");
    test_assert_cursor(3, 1);
    const char *ops[] = {"@", "P", "X"};
    for (unsigned int i = 0; i < sizeof ops / sizeof *ops; i++) {
        char seq[64];
        test_reset_terminal(4, 8);
        test_feed_string("ABCDEFGH\033[1;2H");
        snprintf(seq, sizeof seq, "\033[%d%s", INT_MAX, ops[i]);
        test_feed_string(seq);
        test_assert_cell(0, 0, "A", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
        for (int c = 1; c < 8; c++)
            test_assert_cell(0, c, "", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    }
    alarm(0);
    test_print_ok("parser/large_counts");
    return 0;
}
