#include "../common/test_common.h"

static void blank_row(int row) {
    for (int col = 0; col < term_cols; col++)
        test_assert_cell(row, col, "", 2, 1, ATTR_BOLD);
}

int main(void) {
    const char *moves[] = {
        "\033[3;1H\n", "\033[1;1H\033M", "\033[S", "\033[T",
        "\033[2;1H\033[L", "\033[2;1H\033[M"
    };
    const int rows[] = {2, 0, 2, 0, 1, 2};
    for (unsigned i = 0; i < sizeof moves / sizeof *moves; i++) {
        test_reset_terminal(3, 5);
        test_feed_string("A\r\nB\r\nC\033[1;32;41m");
        test_feed_string(moves[i]);
        blank_row(rows[i]);
        test_assert_attrs(2, 1, ATTR_BOLD);
    }
    test_print_ok("screen/scroll_background");
    return 0;
}
