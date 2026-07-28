#include <string.h>

#include "../common/test_common.h"

static void assert_cluster(int visual, int row, int col, const char *expected,
                           const char *message) {
    char cluster[MAX_CLUSTER_UTF8];
    size_t n = visual
        ? term_render_visual_cluster(row, col, cluster, sizeof(cluster))
        : term_render_cluster(row, col, cluster, sizeof(cluster));

    test_assert_true(n == strlen(expected), message);
    test_assert_true(memcmp(cluster, expected, n + 1) == 0, message);
}

int main(void) {
    static const char a_acute[] = "a\xCC\x81";
    static const char b_acute[] = "b\xCC\x81";

    test_reset_terminal(3, 5);
    test_feed_string(a_acute);
    test_feed_string("\r\nB\r\nC\r\nD");
    kscrollup_n(1);
    assert_cluster(1, 0, 0, a_acute,
        "scrollback must retain combining marks");
    kscrolldown_n(1);

    test_reset_terminal(2, 4);
    test_feed_string(a_acute);
    tresize(3, 6);
    assert_cluster(0, 0, 0, a_acute,
        "primary resize must retain combining marks");

    test_reset_terminal(4, 4);
    test_feed_string("A\r\nB\r\nC\xCC\x81\r\nD");
    tresize(2, 4);
    assert_cluster(0, 0, 0, "C\xCC\x81",
        "shrinking must retain the rows around the cursor like st");
    assert_cluster(0, 1, 0, "D",
        "shrinking must keep the cursor row visible like st");

    test_reset_terminal(2, 4);
    test_feed_string(a_acute);
    test_feed_string("\033[?1049h");
    test_feed_string("\033[H");
    test_feed_string(b_acute);
    tresize(4, 7);
    assert_cluster(0, 0, 0, b_acute,
        "alternate resize must retain combining marks");
    test_feed_string("\033[?1049l");

    test_reset_terminal(1, 6);
    test_feed_string(a_acute);
    test_feed_string("b");
    test_feed_string("\033[1;1H\033[@");
    assert_cluster(0, 0, 1, a_acute,
        "ICH must move combining marks with their base");
    test_feed_string("\033[P");
    assert_cluster(0, 0, 0, a_acute,
        "DCH must move combining marks with their base");

    test_reset_terminal(1, 6);
    test_feed_string(a_acute);
    test_feed_string("b\033[1;1H\033[4hX");
    assert_cluster(0, 0, 1, a_acute,
        "insert mode must move combining marks with their base");
    assert_cluster(0, 0, 0, "X",
        "insert-mode overwrite must not inherit combining marks");

    test_reset_terminal(1, 4);
    test_feed_string(a_acute);
    test_feed_string("\033[1;1H\033[X");
    assert_cluster(0, 0, 0, "",
        "ECH must clear combining marks");
    test_feed_string(b_acute);
    assert_cluster(0, 0, 0, b_acute,
        "overwrite after erase must not resurrect stale marks");

    test_reset_terminal(1, 2);
    test_feed_string("a");
    for (int i = 0; i < MAX_COMBINING_MARKS + 1; i++)
        test_feed_string("\xCC\x81");
    {
        char cluster[MAX_CLUSTER_UTF8];
        size_t n = term_render_cluster(0, 0, cluster, sizeof(cluster));
        test_assert_true(n == 1 + (size_t)MAX_COMBINING_MARKS * 2,
            "cluster must retain exactly the documented combining-mark limit");
        test_assert_true(cluster[n] == '\0',
            "maximum cluster must be NUL terminated");
    }

    test_print_ok("screen/combining_transforms");
    return 0;
}
