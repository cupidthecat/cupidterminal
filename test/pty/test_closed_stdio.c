#define _POSIX_C_SOURCE 200809L
#include "../../src/pty.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
char *utmp, *scroll;
char *stty_args = "stty raw";
static int exercise(int mask) {
    PtySession s = {.master_fd = -1, .child_pid = -1};
    char *args[] = {"/bin/sh", "-c", "printf OUT; printf ERR >&2; read value; test \"$value\" = input", NULL};
    char output[1024] = {0};
    size_t used = 0;
    for (int fd = 0; fd < 3; fd++) if (mask & (1 << fd)) close(fd);
    if (pty_session_spawn(&s, NULL, "/bin/sh", args, "st-256color") < 0) return 1;
    if (pty_session_write(&s, "input\n", 6) < 0) return 2;
    for (int i = 0; i < 200; i++) {
        ssize_t n = pty_session_read(&s, output + used, sizeof output - used - 1);
        if (n > 0) used += (size_t)n;
        if (n == 0 || (n < 0 && errno == EIO)) break;
        if (n < 0 && errno != EAGAIN && errno != EINTR) break;
        struct timespec delay = {0, 10000000}; nanosleep(&delay, NULL);
    }
    pty_session_close(&s);
    return !(strstr(output, "OUT") && strstr(output, "ERR") &&
             s.child_exited && WIFEXITED(s.child_status) && WEXITSTATUS(s.child_status) == 0);
}
int main(void) {
    for (int mask = 0; mask < 8; mask++) {
        pid_t pid = fork();
        if (pid < 0) return 2;
        if (pid == 0) _exit(exercise(mask));
        int status;
        if (waitpid(pid, &status, 0) != pid) return 2;
        if (!WIFEXITED(status) || WEXITSTATUS(status)) {
            fprintf(stderr, "TEST FAILURE: closed standard-fd mask %d\n", mask);
            return 1;
        }
    }
    puts("PASS: pty/closed_standard_descriptors");
    return 0;
}
