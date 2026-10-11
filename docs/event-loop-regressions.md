# Event-loop checks

The event loop uses `poll` for X11 and PTY readiness. It can therefore handle
file descriptors above `FD_SETSIZE` without indexing a fixed-size bitset.

Back-buffer copies disable graphics-exposure notifications. Otherwise each
successful copy produces a NoExpose event that triggers another redraw.
Blinking text has an explicit deadline, so it continues to change while the
terminal is otherwise idle.

`test/x11/test_event_loop.sh` runs three checks under Xvfb:

- Count real back-buffer copies during a quiet interval. The observer forwards
  each XCopyArea call unchanged and adds no display events.
- Sample window pixels over four seconds with the cursor hidden to check that
  blinking text changes more than once.
- Launch with inherited descriptors through 1100 and verify keyboard delivery
  to the child process.

These checks run through `make test-x11`. The count threshold allows a few
startup redraws; it is not a CPU performance benchmark.
