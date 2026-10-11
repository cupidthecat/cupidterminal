#include <stdlib.h>
#include "test_common.h"

int main(void) {
    const int snaps[] = {0, SNAP_WORD, SNAP_LINE};
    for (int type = SEL_REGULAR; type <= SEL_RECTANGULAR; type++) {
        for (size_t n = 0; n < sizeof snaps / sizeof snaps[0]; n++) {
            test_reset_terminal(4, 12);
            test_feed_string("\033[1;10HX");
            selstart(9, 0, snaps[n], type);
            tresize(4, 12);
            test_assert_true(selisactive(), "unchanged dimensions preserve selection");
            tresize(4, 8);
            /* Copy before checking active state so sanitizer detects stale reads. */
            char *copy = getsel();
            test_assert_true(copy == NULL, "stale selection cannot be copied");
            free(copy);
            test_assert_true(!selisactive(), "narrowing clears selection");
            test_assert_true(!selected(0, 0), "no stale highlight");

            selstart(0, 3, snaps[n], type);
            tresize(2, 8);
            test_assert_true(!selisactive(), "shortening clears selection");
        }
    }
    test_print_ok("screen/resize_selection");
    return 0;
}
