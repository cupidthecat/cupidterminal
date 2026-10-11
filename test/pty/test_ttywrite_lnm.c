#define CUPID_NO_MAIN
#define pty_session_write capture_write
#include "../../src/cupid.c"
#undef pty_session_write
#include "../common/test_common.h"

static char captured[4096];
static size_t captured_len;
static int writes, fail_on_write;

ssize_t capture_write(PtySession *session, const void *data, size_t len) {
    (void)session;
    if (++writes == fail_on_write) { errno = EIO; return -1; }
    if (len > sizeof captured - captured_len) abort();
    memcpy(captured + captured_len, data, len);
    captured_len += len;
    return (ssize_t)len;
}

int main(void) {
    const size_t sizes[] = {0, 1, 255, 256, 257, 511, 512, 1025};
    char input[1025], expected[2050];
    test_reset_terminal(3, 8);
    g_pty_session.master_fd = 42; /* All writes are captured, not sent to an fd. */
    for (int mode = 0; mode <= 1; mode++) {
        term.lnm_mode = mode;
        for (unsigned int i = 0; i < sizeof sizes / sizeof *sizes; i++) {
            size_t n = sizes[i], want = 0;
            for (size_t c = 0; c < n; c++) {
                input[c] = c % 3 ? 'x' : '\r';
                expected[want++] = input[c];
                if (mode && input[c] == '\r') expected[want++] = '\n';
            }
            captured_len = 0;
            writes = 0;
            ttywrite(input, n, 0);
            test_assert_true(captured_len == want && !memcmp(captured, expected, want),
                             "LNM expansion must not depend on write length");
        }
    }
    term.lnm_mode = 1;
    captured_len = 0;
    writes = 0;
    fail_on_write = 2;
    ttywrite(input, sizeof input, 0);
    test_assert_true(writes == 2 && captured_len > 0 && captured_len <= 512,
                     "stop at the failed chunk without submitting later bytes");
    fail_on_write = 0;
    term.echo_mode = 1;
    ttywrite("Q", 1, 1);
    test_assert_cursor(0, 1);
    test_print_ok("pty/ttywrite_lnm");
    return 0;
}
