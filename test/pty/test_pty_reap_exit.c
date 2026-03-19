#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../src/pty_session.h"

static void fail(const char *msg) {
    fprintf(stderr, "TEST FAILURE: %s\n", msg);
    exit(EXIT_FAILURE);
}

static void sleep_10ms(void) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 10 * 1000 * 1000;
    nanosleep(&ts, NULL);
}

int main(void) {
    PtySession session = {
        .master_fd = -1,
        .child_pid = -1,
        .child_exited = 0,
        .child_status = 0,
    };
    const char *exit_cmd = "exit 7\n";
    int status = 0;
    int reaped = 0;

    if (pty_session_spawn(&session, "/bin/sh", "xterm-256color") == -1) {
        fail("pty_session_spawn failed");
    }

    if (pty_session_write(&session, exit_cmd, strlen(exit_cmd)) == -1) {
        pty_session_close(&session);
        fail("pty_session_write failed");
    }

    for (int i = 0; i < 400 && !reaped; i++) {
        int rc = pty_session_reap_child(&session, &status);
        if (rc == 1) {
            reaped = 1;
            break;
        }
        if (rc == -1) {
            pty_session_close(&session);
            fail("pty_session_reap_child failed");
        }
        sleep_10ms();
    }

    if (!reaped) {
        pty_session_close(&session);
        fail("did not reap shell child after exit command");
    }

    if (!session.child_exited || session.child_pid != -1) {
        pty_session_close(&session);
        fail("session child lifecycle fields not updated");
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 7) {
        pty_session_close(&session);
        fail("unexpected exit status from shell child");
    }

    pty_session_close(&session);

    printf("PASS: pty/reap_exit_status\n");
    return 0;
}
