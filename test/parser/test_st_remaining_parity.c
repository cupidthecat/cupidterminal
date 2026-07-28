#define _POSIX_C_SOURCE 200809L

#include "../common/test_common.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    uint8_t bytes[128];
    size_t len;
} ResponseCapture;

static void capture_response(const uint8_t *bytes, size_t len, void *ctx) {
    ResponseCapture *capture = ctx;
    if (len > sizeof capture->bytes)
        len = sizeof capture->bytes;
    memcpy(capture->bytes, bytes, len);
    capture->len = len;
}

int main(void) {
    ResponseCapture response = {{0}, 0};
    char printer_path[] = "/tmp/cupidterminal-printer-parity.XXXXXX";
    char output[32] = {0};
    int fd;

    test_reset_terminal(4, 4);
    test_feed_string("\033[2;4r\033[?6hABCD\0337\033[?6l\033[1;1H\0338X");
    test_assert_mode("restored origin_mode", term.origin_mode, 1);
    test_assert_cell(2, 0, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    test_reset_terminal(2, 8);
    test_feed_string("\033]0;both\a");
    test_assert_true(strcmp(test_last_title(), "both") == 0,
        "OSC 0 must set the window title");
    test_assert_true(strcmp(test_last_icon_title(), "both") == 0,
        "OSC 0 must set the icon title");
    test_feed_string("\033]1;icon\a");
    test_assert_true(strcmp(test_last_icon_title(), "icon") == 0,
        "OSC 1 must set only the icon title");
    test_assert_true(strcmp(test_last_title(), "both") == 0,
        "OSC 1 must not change the window title");
    test_feed_string("\033klegacy\033\\");
    test_assert_true(strcmp(test_last_title(), "legacy") == 0,
        "ESC k must set the legacy window title");

    response.len = 0;
    twrite((const uint8_t *)"\033]10;?\a", 7, &term,
           capture_response, &response);
    test_assert_true(response.len == strlen("\033]10;rgb:e5e5/e5e5/e5e5\a"),
        "OSC 10 query response length mismatch");
    test_assert_true(memcmp(response.bytes, "\033]10;rgb:e5e5/e5e5/e5e5\a",
                            response.len) == 0,
        "OSC 10 query response mismatch");
    response.len = 0;
    twrite((const uint8_t *)"\033]4;3;?\a", 8, &term,
           capture_response, &response);
    test_assert_true(response.len == strlen("\033]4;3;rgb:0000/0000/0000\a") &&
                     memcmp(response.bytes, "\033]4;3;rgb:0000/0000/0000\a",
                            response.len) == 0,
        "OSC 4 query response mismatch");

    test_feed_string("\033[1;58;5;2;4m");
    test_assert_attrs(COLOR_DEFAULT_FG, COLOR_DEFAULT_BG,
                      ATTR_BOLD | ATTR_UNDERLINE);
    test_feed_string("\033[38;5;999m");
    test_assert_true(term.current_fg == COLOR_DEFAULT_FG,
        "invalid indexed colors must be rejected, not clamped");

    test_feed_string("\033]10;#010203\a\033]4;3;#040506\a\033]2;changed\a\033c");
    test_assert_true(term.osc_fg_color == 0 && !term.palette_overridden[3],
        "RIS must reset dynamic colors");
    test_assert_true(strcmp(test_last_title(), "cupidterminal") == 0,
        "RIS must reset the title");
    test_feed_string("\033]4;3;#040506\a\033]104\a");
    test_assert_true(!term.palette_overridden[3],
        "OSC 104 without a parameter must reset the whole palette");

    test_reset_terminal(2, 5);
    test_feed_string("AB");
    fd = mkstemp(printer_path);
    test_assert_true(fd >= 0, "printer fixture creation failed");
    test_assert_true(write(fd, "123456789", 9) == 9,
        "printer fixture write failed");
    close(fd);
    test_assert_true(tprinter_open(printer_path) == 0,
        "printer output could not be opened");
    tprinter_screen();
    tprinter_close();
    fd = open(printer_path, O_RDONLY);
    test_assert_true(fd >= 0, "printer output could not be read");
    test_assert_true(read(fd, output, sizeof output) == 9,
        "printer open must not truncate an existing target");
    close(fd);
    unlink(printer_path);
    test_assert_true(memcmp(output, "AB\n\n56789", 9) == 0,
        "printer screen dump must use logical line length without truncation");

    test_print_ok("parser/st_remaining_parity");
    return 0;
}
