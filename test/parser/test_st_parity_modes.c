#define _POSIX_C_SOURCE 200809L

#include "../common/test_common.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int allowaltscreen;

int main(void) {
    char oversized_osc[700];
    char printer_path[] = "/tmp/cupidterminal-printer.XXXXXX";
    char printer_output[32] = {0};
    static const char expected_printer[] = "HELLO\033[4i";
    int printer_test_fd;
    test_reset_terminal(3, 6);

    test_feed_string("\033=");
    test_assert_mode("application_keypad", term.application_keypad, 1);
    test_feed_string("\033>");
    test_assert_mode("application_keypad", term.application_keypad, 0);

    test_feed_string("\033[?9h\033[?1034h\033[2h");
    test_assert_mode("mouse_reporting_x10", term.mouse_reporting_x10, 1);
    test_assert_mode("meta_eight_bit", term.meta_eight_bit, 1);
    test_assert_mode("keyboard_lock", term.keyboard_lock, 1);
    test_feed_string("\033[?9l\033[?1034l\033[2l");
    test_assert_mode("mouse_reporting_x10", term.mouse_reporting_x10, 0);
    test_assert_mode("meta_eight_bit", term.meta_eight_bit, 0);
    test_assert_mode("keyboard_lock", term.keyboard_lock, 0);

    test_feed_string("\033[5 q");
    test_assert_mode("cursorshape", term.cursorshape, 5);

    test_feed_string("\033[38:2:255:0:128m");
    test_assert_true(term.current_fg == TRUECOLOR(255, 0, 128),
        "colon-delimited true color mismatch");

    test_reset_terminal(3, 6);
    test_feed_string("\033[2;3H");
    allowaltscreen = 0;
    test_feed_string("\033[?1049h\033[?1049l");
    test_assert_mode("alt_screen_active", term.alt_screen_active, 0);
    test_assert_cursor(1, 2);
    allowaltscreen = 1;

    test_feed_string("\033[?1049h\033[3;4H\0337\033[1;1H\033[?1049l");
    test_assert_cursor(1, 2);

    test_feed_string("\033[?47h");
    test_assert_cursor(1, 2);
    test_feed_string("Z\033[?47l");
    test_assert_cursor(1, 3);

    test_feed_string("\033#8");
    for (int row = 0; row < term_rows; row++) {
        for (int col = 0; col < term_cols; col++) {
            test_assert_cell(row, col, "E", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
        }
    }

    test_reset_terminal(3, 6);
    test_feed_string("\033*0\033nq");
    test_assert_cell(0, 0, "\342\224\200", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);
    test_feed_string("\033+B\033oX");
    test_assert_cell(0, 1, "X", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG, 0);

    memset(oversized_osc, 'x', sizeof(oversized_osc));
    memcpy(oversized_osc, "\033]2;", 4);
    oversized_osc[sizeof(oversized_osc) - 2] = '\a';
    oversized_osc[sizeof(oversized_osc) - 1] = '\0';
    test_feed_string("\033]2;safe\a");
    test_feed_string(oversized_osc);
    test_assert_true(strcmp(term.window_title, "safe") == 0,
        "truncated OSC must not be executed");

    printer_test_fd = mkstemp(printer_path);
    test_assert_true(printer_test_fd >= 0, "printer test file creation failed");
    close(printer_test_fd);
    test_assert_true(tprinter_open(printer_path) == 0,
        "printer output could not be opened");
    term.print_mode = 0;
    test_feed_string("\033[5");
    test_feed_string("iHELLO\033[4");
    test_feed_string("iTAIL");
    tprinter_close();
    printer_test_fd = open(printer_path, O_RDONLY);
    test_assert_true(printer_test_fd >= 0, "printer test output could not be read");
    test_assert_true(read(printer_test_fd, printer_output,
        sizeof(printer_output)) == (ssize_t)(sizeof(expected_printer) - 1),
        "printer output length mismatch");
    close(printer_test_fd);
    unlink(printer_path);
    test_assert_true(memcmp(printer_output, expected_printer,
        sizeof(expected_printer) - 1) == 0,
        "printer mode did not follow split MC sequences");

    test_print_ok("parser/st_parity_modes");
    return 0;
}
