# Child exit and event-loop wakeups

A reaped child is no longer treated as an open serial session. Both use a
negative child PID, so liveness must also check the recorded exit flag.

SIGCHLD writes to a non-blocking, close-on-exec pipe. The X11 loop polls its read
end alongside the display and PTY descriptors. This keeps a child-exit
notification observable when it arrives just before the loop starts waiting.
The signal handler preserves errno and never waits for pipe space.

Before shutting down an exited session, the loop processes PTY output already
queued. It does not wait for a background descendant to close the slave.

Tests cover a real shell exit, a signal delivered before polling, serial-session
liveness, and a window whose immediate child exits while a descendant still
holds the PTY open. The window test runs under Xvfb in CI.
