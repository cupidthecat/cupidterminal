#include <string.h>

#include "../common/test_common.h"

int main(void) {
    test_reset_terminal(4, 8);
    test_feed_string("abc");

    char *snapshot = test_snapshot_screen();
    test_assert_true(strncmp(snapshot, "abc     \n", 8) == 0, "snapshot first line mismatch");
    test_free_snapshot(snapshot);

    test_feed_string("\x1b[s\x1b[4;8HZ\x1b[uQ");
    test_assert_cell(0, 3, "Q", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_assert_cell(3, 7, "Z", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_print_ok("screen/snapshot_cursor_save");
    return 0;
}
