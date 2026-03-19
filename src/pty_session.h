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

int pty_session_spawn(PtySession *session, const char *shell_path, const char *term_name);
int pty_session_set_winsize(PtySession *session, unsigned short rows, unsigned short cols);
int pty_session_forward_winsize_from_fd(PtySession *session, int source_fd);
int pty_session_reap_child(PtySession *session, int *status_out);
int pty_session_child_alive(const PtySession *session);
ssize_t pty_session_read(PtySession *session, void *buf, size_t len);
ssize_t pty_session_write(PtySession *session, const void *buf, size_t len);
void pty_session_close(PtySession *session);

#endif // PTY_SESSION_H