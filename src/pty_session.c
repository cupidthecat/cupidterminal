#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "pty_session.h"

static void pty_session_mark_exited(PtySession *session, int status) {
    if (!session) {
        return;
    }
    session->child_exited = 1;
    session->child_status = status;
    session->child_pid = -1;
}

static void pty_session_signal_child_group(PtySession *session, int sig) {
    if (!session || session->child_pid <= 0) {
        return;
    }

    if (kill(-session->child_pid, sig) == -1) {
        if (errno == EPERM || errno == ESRCH) {
            (void)kill(session->child_pid, sig);
        }
    }
}

static int pty_session_wait_for_reap(PtySession *session, int timeout_ms) {
    const int interval_ms = 20;
    struct timespec ts;
    int elapsed_ms = 0;

    ts.tv_sec = 0;
    ts.tv_nsec = interval_ms * 1000 * 1000;

    while (elapsed_ms <= timeout_ms) {
        int rc = pty_session_reap_child(session, NULL);
        if (rc != 0) {
            return rc;
        }

        if (elapsed_ms == timeout_ms) {
            break;
        }

        nanosleep(&ts, NULL);
        elapsed_ms += interval_ms;
    }

    return 0;
}

int pty_session_spawn(PtySession *session, const char *shell_path, const char *term_name) {
    int slave_fd = -1;
    pid_t pid;

    if (!session || !shell_path || !term_name) {
        return -1;
    }

    if (session->master_fd >= 0 || session->child_pid > 0) {
        pty_session_close(session);
    }

    session->master_fd = -1;
    session->child_pid = -1;
    session->child_exited = 0;
    session->child_status = 0;

    if (setenv("TERM", term_name, 1) == -1) {
        perror("setenv TERM failed");
        return -1;
    }

    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd == -1) {
        perror("posix_openpt failed");
        return -1;
    }

    if (grantpt(master_fd) == -1 || unlockpt(master_fd) == -1) {
        perror("grantpt/unlockpt failed");
        close(master_fd);
        return -1;
    }

    char *slave_name = ptsname(master_fd);
    if (!slave_name) {
        perror("ptsname failed");
        close(master_fd);
        return -1;
    }

    slave_fd = open(slave_name, O_RDWR | O_NOCTTY);
    if (slave_fd == -1) {
        perror("open slave pty failed");
        close(master_fd);
        return -1;
    }

    if (fcntl(master_fd, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl O_NONBLOCK failed");
        close(slave_fd);
        close(master_fd);
        return -1;
    }

    pid = fork();
    if (pid == -1) {
        perror("fork failed");
        close(slave_fd);
        close(master_fd);
        return -1;
    }

    if (pid == 0) {
        close(master_fd);

        if (setsid() == -1) {
            perror("setsid failed");
            _exit(EXIT_FAILURE);
        }

        if (ioctl(slave_fd, TIOCSCTTY, 0) == -1) {
            perror("ioctl TIOCSCTTY failed");
            _exit(EXIT_FAILURE);
        }

        if (tcsetpgrp(slave_fd, getpgrp()) == -1) {
            perror("tcsetpgrp failed");
            _exit(EXIT_FAILURE);
        }

        /* Use kernel default termios (cooked + echo). The shell will reconfigure
         * for raw mode when it needs line editing. Explicit cfmakeraw caused
         * "nothing until Enter" because the shell expects to start from default. */

        signal(SIGCHLD, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGALRM, SIG_DFL);
        signal(SIGWINCH, SIG_DFL);

        if (dup2(slave_fd, STDIN_FILENO) == -1 ||
            dup2(slave_fd, STDOUT_FILENO) == -1 ||
            dup2(slave_fd, STDERR_FILENO) == -1) {
            perror("dup2 failed");
            _exit(EXIT_FAILURE);
        }

        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }

        execlp(shell_path, shell_path, (char *)NULL);
        perror("execlp failed");
        _exit(EXIT_FAILURE);
    }

    close(slave_fd);
    session->master_fd = master_fd;
    session->child_pid = pid;
    session->child_exited = 0;
    session->child_status = 0;
    return 0;
}

int pty_session_set_winsize(PtySession *session, unsigned short rows, unsigned short cols) {
    if (!session || session->master_fd < 0) {
        return -1;
    }

    if (rows == 0) {
        rows = 1;
    }
    if (cols == 0) {
        cols = 1;
    }

    struct winsize ws;
    ws.ws_row = rows;
    ws.ws_col = cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    return ioctl(session->master_fd, TIOCSWINSZ, &ws);
}

int pty_session_forward_winsize_from_fd(PtySession *session, int source_fd) {
    if (!session || session->master_fd < 0) {
        return -1;
    }

    struct winsize ws;
    if (ioctl(source_fd, TIOCGWINSZ, &ws) == -1) {
        return -1;
    }

    return ioctl(session->master_fd, TIOCSWINSZ, &ws);
}

int pty_session_reap_child(PtySession *session, int *status_out) {
    int status = 0;
    pid_t rc;

    if (!session) {
        return -1;
    }

    if (session->child_pid <= 0) {
        return 0;
    }

    do {
        rc = waitpid(session->child_pid, &status, WNOHANG);
    } while (rc == -1 && errno == EINTR);

    if (rc == 0) {
        return 0;
    }

    if (rc == -1) {
        if (errno == ECHILD) {
            pty_session_mark_exited(session, status);
            if (status_out) {
                *status_out = session->child_status;
            }
            return 1;
        }
        return -1;
    }

    pty_session_mark_exited(session, status);
    if (status_out) {
        *status_out = status;
    }
    return 1;
}

int pty_session_child_alive(const PtySession *session) {
    return session && session->child_pid > 0 && !session->child_exited;
}

ssize_t pty_session_read(PtySession *session, void *buf, size_t len) {
    if (!session || session->master_fd < 0 || !buf) {
        return -1;
    }
    return read(session->master_fd, buf, len);
}

ssize_t pty_session_write(PtySession *session, const void *buf, size_t len) {
    if (!session || session->master_fd < 0 || !buf) {
        return -1;
    }
    return write(session->master_fd, buf, len);
}

void pty_session_close(PtySession *session) {
    int rc;

    if (!session) {
        return;
    }

    if (session->master_fd >= 0) {
        close(session->master_fd);
        session->master_fd = -1;
    }

    rc = pty_session_wait_for_reap(session, 0);
    if (rc == 0) {
        pty_session_signal_child_group(session, SIGHUP);
        rc = pty_session_wait_for_reap(session, 200);
    }
    if (rc == 0) {
        pty_session_signal_child_group(session, SIGTERM);
        rc = pty_session_wait_for_reap(session, 400);
    }
    if (rc == 0) {
        pty_session_signal_child_group(session, SIGKILL);
        (void)pty_session_wait_for_reap(session, 400);
    }
}