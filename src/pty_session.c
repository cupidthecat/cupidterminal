/*
 * pty_session.c – PTY / serial-line session management.
 *
 * Implements st-compatible PTY spawning, execsh environment setup, serial line
 * support, utmp/scroll wrapper, and write flow control.
 */
#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "pty_session.h"

/* Extern variables from main.c / config.h -----------------------------------*/
extern char *utmp;        /* utmp(1) wrapper program, or NULL             */
extern char *scroll;      /* scroll(1) wrapper program, or NULL           */
extern char *stty_args;   /* stty argument string for serial-line mode    */

/* ---------------------------------------------------------------------------*/

static void pty_session_mark_exited(PtySession *session, int status) {
    if (!session) return;
    session->child_exited = 1;
    session->child_status = status;
    session->child_pid = -1;
}

static void pty_session_signal_child_group(PtySession *session, int sig) {
    if (!session || session->child_pid <= 0) return;
    if (kill(-session->child_pid, sig) == -1) {
        if (errno == EPERM || errno == ESRCH)
            (void)kill(session->child_pid, sig);
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
        if (rc != 0) return rc;
        if (elapsed_ms == timeout_ms) break;
        nanosleep(&ts, NULL);
        elapsed_ms += interval_ms;
    }
    return 0;
}

/*
 * Run stty_args via system(3) on the current terminal (stdin = slave pty /
 * serial device).  Only used in serial-line mode, matching st's stty().
 */
static void run_stty(char **extra_args) {
    char cmd[4096];
    const char *base = stty_args ? stty_args : "stty raw pass8 nl -echo -iexten -cstopb 38400";
    size_t n = strlen(base);
    if (n >= sizeof(cmd) - 1) return;
    memcpy(cmd, base, n);
    cmd[n] = '\0';

    if (extra_args) {
        for (char **p = extra_args; *p; p++) {
            size_t slen = strlen(*p);
            if (n + 1 + slen >= sizeof(cmd) - 1) break;
            cmd[n++] = ' ';
            memcpy(cmd + n, *p, slen);
            n += slen;
            cmd[n] = '\0';
        }
    }

    if (system(cmd) != 0)
        perror("stty");
}

/*
 * execsh – resolve the program to exec and set up the child environment,
 * mirroring st's execsh() exactly:
 *   - resolve shell from SHELL env → pw_shell → shell_path fallback
 *   - if args given: exec them directly
 *   - elif scroll set: exec scroll with utmp/sh as argument
 *   - elif utmp set: exec utmp directly
 *   - else: exec shell
 *   - unset COLUMNS, LINES, TERMCAP
 *   - set LOGNAME, USER, SHELL, HOME from passwd entry
 *   - TERM is already set before fork
 */
static void execsh(const char *shell_path, char **args) {
    const struct passwd *pw;
    char *sh;
    char *prog;
    char *arg = NULL;
    char *constructed[3];

    errno = 0;
    pw = getpwuid(getuid());
    if (pw == NULL) {
        if (errno)
            perror("getpwuid");
        else
            fprintf(stderr, "who are you?\n");
        _exit(EXIT_FAILURE);
    }

    sh = getenv("SHELL");
    if (!sh || sh[0] == '\0')
        sh = (pw->pw_shell[0]) ? pw->pw_shell : (char *)shell_path;

    if (args) {
        prog = args[0];
        arg  = NULL;
    } else if (scroll) {
        prog = scroll;
        arg  = utmp ? utmp : sh;
    } else if (utmp) {
        prog = utmp;
        arg  = NULL;
    } else {
        prog = sh;
        arg  = NULL;
    }

    if (!args) {
        constructed[0] = prog;
        constructed[1] = arg;
        constructed[2] = NULL;
        args = constructed;
    }

    /* Match st: clean the environment before exec */
    unsetenv("COLUMNS");
    unsetenv("LINES");
    unsetenv("TERMCAP");
    setenv("LOGNAME", pw->pw_name, 1);
    setenv("USER",    pw->pw_name, 1);
    setenv("SHELL",   sh,          1);
    setenv("HOME",    pw->pw_dir,  1);

    execvp(prog, args);
    perror("execvp");
    _exit(EXIT_FAILURE);
}

/* ---------------------------------------------------------------------------
 * pty_session_spawn
 *
 * If line != NULL: serial-line mode – open the device, run stty, no fork.
 * Otherwise:       PTY mode – posix_openpt (O_CLOEXEC), fork, execsh.
 * ---------------------------------------------------------------------------*/
int pty_session_spawn(PtySession *session, const char *line,
                      const char *shell_path, char **args,
                      const char *term_name) {
    if (!session || !shell_path || !term_name) return -1;

    if (session->master_fd >= 0 || session->child_pid > 0)
        pty_session_close(session);

    session->master_fd    = -1;
    session->child_pid    = -1;
    session->child_exited = 0;
    session->child_status = 0;

    if (setenv("TERM", term_name, 1) == -1) {
        perror("setenv TERM");
        return -1;
    }

    /* ── Serial-line mode ──────────────────────────────────────────────── */
    if (line) {
        int fd = open(line, O_RDWR | O_NOCTTY);
        if (fd < 0) {
            perror("open serial line");
            return -1;
        }
        /* Mirror st: dup the line to stdin so stty configures it */
        if (dup2(fd, STDIN_FILENO) == -1) {
            perror("dup2 serial line");
            close(fd);
            return -1;
        }
        run_stty(args);
        /* In serial mode there is no child process; fd is our "master" */
        if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
            perror("fcntl O_NONBLOCK (serial)");
            close(fd);
            return -1;
        }
        session->master_fd = fd;
        /* No child process in serial mode */
        return 0;
    }

    /* ── PTY mode ──────────────────────────────────────────────────────── */

    /* O_CLOEXEC prevents the master fd from leaking across exec in grandchildren */
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (master_fd < 0) {
        perror("posix_openpt");
        return -1;
    }

    if (grantpt(master_fd) < 0 || unlockpt(master_fd) < 0) {
        perror("grantpt/unlockpt");
        close(master_fd);
        return -1;
    }

    /* ptsname_r is thread-safe; ptsname is not */
    char slave_name[256];
    if (ptsname_r(master_fd, slave_name, sizeof(slave_name)) != 0) {
        perror("ptsname_r");
        close(master_fd);
        return -1;
    }

    /* Open slave with O_CLOEXEC; the child will close it after dup2 anyway,
       but the flag makes accidental leaks impossible. */
    int slave_fd = open(slave_name, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (slave_fd < 0) {
        perror("open slave pty");
        close(master_fd);
        return -1;
    }

    /* Non-blocking on the master (parent side only; child closes master) */
    if (fcntl(master_fd, F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl O_NONBLOCK");
        close(slave_fd);
        close(master_fd);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(slave_fd);
        close(master_fd);
        return -1;
    }

    if (pid == 0) {
        /* ── Child – mirrors st's ttynew() child path exactly ───────────── */
        close(master_fd);

        setsid(); /* create a new process group */

        /* dup slave to stdin/stdout/stderr before TIOCSCTTY (matches st) */
        if (dup2(slave_fd, STDIN_FILENO)  < 0 ||
            dup2(slave_fd, STDOUT_FILENO) < 0 ||
            dup2(slave_fd, STDERR_FILENO) < 0) {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }

        if (ioctl(slave_fd, TIOCSCTTY, NULL) < 0) {
            perror("TIOCSCTTY");
            _exit(EXIT_FAILURE);
        }

        if (slave_fd > STDERR_FILENO)
            close(slave_fd);

        /* Restore all signals to default before exec */
        signal(SIGCHLD,  SIG_DFL);
        signal(SIGHUP,   SIG_DFL);
        signal(SIGINT,   SIG_DFL);
        signal(SIGQUIT,  SIG_DFL);
        signal(SIGTERM,  SIG_DFL);
        signal(SIGALRM,  SIG_DFL);
        signal(SIGWINCH, SIG_DFL);

        execsh(shell_path, args);
        /* execsh does _exit on failure */
    }

    /* ── Parent ─────────────────────────────────────────────────────────── */
    close(slave_fd);
    session->master_fd    = master_fd;
    session->child_pid    = pid;
    session->child_exited = 0;
    session->child_status = 0;
    return 0;
}

/* ---------------------------------------------------------------------------
 * pty_session_write – write bytes to the PTY master fd.
 *
 * Uses a simple retry loop. The master fd is O_NONBLOCK so write() returns
 * EAGAIN rather than blocking; we break out and let the main event loop drain
 * the PTY before the caller retries. Draining inside write() would silently
 * discard shell/program output (corrupting escape sequences), so we do not
 * attempt to read the fd here — that is the main loop's job.
 * ---------------------------------------------------------------------------*/
ssize_t pty_session_write(PtySession *session, const void *buf, size_t len) {
    const char *s = (const char *)buf;
    size_t remaining = len;

    if (!session || session->master_fd < 0 || !buf) return -1;
    if (len == 0) return 0;

    while (remaining > 0) {
        ssize_t r = write(session->master_fd, s, remaining);
        if (r > 0) {
            s         += r;
            remaining -= (size_t)r;
        } else if (r < 0 && errno == EINTR) {
            continue;
        } else {
            break; /* EAGAIN or error — caller retries on next iteration */
        }
    }

    return (ssize_t)(len - remaining);
}

/* ---------------------------------------------------------------------------*/

int pty_session_set_winsize(PtySession *session, unsigned short rows, unsigned short cols) {
    struct winsize ws;

    if (!session || session->master_fd < 0) return -1;
    if (rows == 0) rows = 1;
    if (cols == 0) cols = 1;

    ws.ws_row    = rows;
    ws.ws_col    = cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    return ioctl(session->master_fd, TIOCSWINSZ, &ws);
}

int pty_session_forward_winsize_from_fd(PtySession *session, int source_fd) {
    struct winsize ws;

    if (!session || session->master_fd < 0) return -1;
    if (ioctl(source_fd, TIOCGWINSZ, &ws) < 0) return -1;
    return ioctl(session->master_fd, TIOCSWINSZ, &ws);
}

int pty_session_reap_child(PtySession *session, int *status_out) {
    int status = 0;
    pid_t rc;

    if (!session) return -1;
    if (session->child_pid <= 0) return 0;

    do {
        rc = waitpid(session->child_pid, &status, WNOHANG);
    } while (rc < 0 && errno == EINTR);

    if (rc == 0) return 0;

    if (rc < 0) {
        if (errno == ECHILD) {
            pty_session_mark_exited(session, status);
            if (status_out) *status_out = session->child_status;
            return 1;
        }
        return -1;
    }

    pty_session_mark_exited(session, status);
    if (status_out) *status_out = status;
    return 1;
}

int pty_session_child_alive(const PtySession *session) {
    /* In serial-line mode child_pid is -1; the "session" lives while the fd is open */
    if (!session) return 0;
    if (session->master_fd < 0) return 0;
    if (session->child_pid < 0) return 1; /* serial mode: fd open = alive */
    return !session->child_exited;
}

ssize_t pty_session_read(PtySession *session, void *buf, size_t len) {
    if (!session || session->master_fd < 0 || !buf) return -1;
    return read(session->master_fd, buf, len);
}

void pty_session_close(PtySession *session) {
    int rc;

    if (!session) return;

    if (session->master_fd >= 0) {
        close(session->master_fd);
        session->master_fd = -1;
    }

    if (session->child_pid <= 0) return;

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
