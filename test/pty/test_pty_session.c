#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "../../src/pty_session.h"

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void sleep_10ms(void) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 10 * 1000 * 1000;
    nanosleep(&ts, NULL);
}

int main(void) {
    PtySession session = { .master_fd = -1, .child_pid = -1 };
    const char *payload = "phase1-pty-check\n";
    const char *marker = "phase1-pty-check";
    char out[1024] = {0};
    size_t used = 0;
    int seen = 0;
    struct winsize ws;
    int reaped = 0;

    if (pty_session_spawn(&session, "/bin/cat", "xterm-256color") == -1) {
        die("pty_session_spawn");
    }

    if (session.master_fd < 0 || session.child_pid <= 0) {
        fprintf(stderr, "TEST FAILURE: invalid PTY session state\n");
        return EXIT_FAILURE;
    }

    if (pty_session_set_winsize(&session, 24, 80) == -1) {
        die("pty_session_set_winsize");
    }

    if (ioctl(session.master_fd, TIOCGWINSZ, &ws) == -1) {
        die("ioctl TIOCGWINSZ");
    }
    if (ws.ws_row != 24 || ws.ws_col != 80) {
        fprintf(stderr, "TEST FAILURE: PTY winsize did not apply\n");
        pty_session_close(&session);
        return EXIT_FAILURE;
    }

    if (pty_session_write(&session, payload, strlen(payload)) == -1) {
        die("pty_session_write");
    }

    for (int i = 0; i < 50 && !seen; i++) {
        fd_set rfds;
        struct timeval tv;

        FD_ZERO(&rfds);
        FD_SET(session.master_fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int rc = select(session.master_fd + 1, &rfds, NULL, NULL, &tv);
        if (rc == -1) {
            if (errno == EINTR) {
                continue;
            }
            die("select");
        }
        if (rc == 0 || !FD_ISSET(session.master_fd, &rfds)) {
            continue;
        }

        ssize_t n = pty_session_read(&session, out + used, sizeof(out) - used - 1);
        if (n > 0) {
            used += (size_t)n;
            out[used] = '\0';
            if (strstr(out, marker) != NULL) {
                seen = 1;
            }
        }
    }

    if (!seen) {
        fprintf(stderr, "TEST FAILURE: did not observe echoed payload\n");
        pty_session_close(&session);
        return EXIT_FAILURE;
    }

    kill(session.child_pid, SIGTERM);
    for (int i = 0; i < 100 && !reaped; i++) {
        int rc = pty_session_reap_child(&session, NULL);
        if (rc == 1) {
            reaped = 1;
            break;
        }
        if (rc == -1) {
            die("pty_session_reap_child");
        }
        sleep_10ms();
    }

    if (!reaped || !session.child_exited || session.child_pid != -1) {
        fprintf(stderr, "TEST FAILURE: child lifecycle tracking/reap failed\n");
        pty_session_close(&session);
        return EXIT_FAILURE;
    }

    pty_session_close(&session);

    if (session.master_fd != -1 || session.child_pid != -1) {
        fprintf(stderr, "TEST FAILURE: session close semantics failed\n");
        return EXIT_FAILURE;
    }

    printf("PASS: pty/session_basic\n");
    return 0;
}
