#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "../../src/pty.h"

char *utmp = NULL;
char *scroll = NULL;
char *stty_args = NULL;
static void check(int ok, const char *message) {
    if (!ok) { fprintf(stderr, "TEST FAILURE: %s\n", message); exit(1); }
}
int main(void) {
    PtySession session = {.master_fd = -1, .child_pid = -1};
    char *args[] = {"/bin/sh", "-c", "exit 0", NULL};
    struct timespec pause = {.tv_nsec = 100000000};
    pty_install_signal_handlers();
    check(pty_signal_fd() >= 0, "signal descriptor available");
    check(pty_session_spawn(&session, NULL, "/bin/sh", args, "st-256color") == 0, "spawn");
    /* Allow SIGCHLD to arrive before poll, exercising the lost-wakeup window. */
    while (nanosleep(&pause, &pause) < 0 && errno == EINTR) {}
    struct pollfd event = {.fd = pty_signal_fd(), .events = POLLIN};
    int ready;
    do { ready = poll(&event, 1, 2000); } while (ready < 0 && errno == EINTR);
    check(ready == 1 && (event.revents & POLLIN), "SIGCHLD stays observable before poll");
    pty_reap(&session);
    check(session.child_exited && !pty_session_child_alive(&session), "reaped session is dead");
    check(poll(&event, 1, 0) == 0, "signal notification drained");
    pty_session_close(&session);
    session.master_fd = dup(STDIN_FILENO);
    session.child_exited = 0;
    check(pty_session_child_alive(&session), "open serial session remains alive");
    pty_session_close(&session);
    puts("PASS: pty/sigchld_wakeup");
    return 0;
}
