#include "../../src/pty.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

char *utmp = NULL;
char *scroll = NULL;
char *stty_args = "stty raw";

int main(void) {
    char command[16];
    char *valid[] = {"echo", NULL};
    char *too_long[] = {"123456789", NULL};

    if (pty_build_stty_command(command, sizeof command, stty_args, valid) != 0 ||
        strcmp(command, "stty raw echo") != 0) {
        fprintf(stderr, "TEST FAILURE: valid stty command construction\n");
        return 1;
    }
    errno = 0;
    if (pty_build_stty_command(command, sizeof command, stty_args, too_long) != -1 ||
        errno != E2BIG) {
        fprintf(stderr, "TEST FAILURE: oversized stty command was truncated\n");
        return 1;
    }
    puts("PASS: pty/stty_command");
    return 0;
}
