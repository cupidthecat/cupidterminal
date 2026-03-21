#ifndef PTY_SESSION_H
#define PTY_SESSION_H

#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
    int master_fd;
    pid_t child_pid;
    int child_exited;
    int child_status;
} PtySession;

/*
 * Spawn a shell (or custom command) in a new PTY session.
 *
 * line       – serial device path (e.g. "/dev/ttyS0"), or NULL for PTY mode.
 * shell_path – fallback shell if SHELL env and passwd entry are absent.
 * args       – custom argv to exec (opt_cmd), or NULL to run shell/utmp/scroll.
 * term_name  – value for the TERM environment variable.
 *
 * On success sets session->master_fd and session->child_pid and returns 0.
 * In serial-line mode child_pid is -1 (no fork).
 */
int pty_session_spawn(PtySession *session, const char *line,
                      const char *shell_path, char **args,
                      const char *term_name);

int pty_session_set_winsize(PtySession *session, unsigned short rows, unsigned short cols);
int pty_session_forward_winsize_from_fd(PtySession *session, int source_fd);
int pty_session_reap_child(PtySession *session, int *status_out);
int pty_session_child_alive(const PtySession *session);
ssize_t pty_session_read(PtySession *session, void *buf, size_t len);
ssize_t pty_session_write(PtySession *session, const void *buf, size_t len);
void pty_session_close(PtySession *session);

#endif /* PTY_SESSION_H */
