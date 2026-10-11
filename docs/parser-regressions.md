# Parser boundary regressions

The parser tests cover large numeric parameters, combined mode changes,
scrollback erasure, and OSC string boundaries. They run through `make test-all`.

Large cursor and edit counts are bounded before arithmetic. Tab movement stops
at the screen edge even when the requested count is `INT_MAX`. The large-count
test has a five-second alarm so an unbounded loop fails rather than hanging CI.

`CSI 3 J` discards saved lines and returns the viewport to the live screen without
erasing live cells. `CSI 2 J` erases live cells while retaining history. The
[xterm control-sequence reference](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)
distinguishes these two operations.

Setting or resetting keyboard lock alongside other ANSI modes applies every
recognized parameter. Unknown modes do not stop processing the rest of the list.

OSC and ignored control strings track UTF-8 continuation bytes across reads, so
byte `0x9c` inside a multibyte character is not mistaken for a string terminator.
Standalone 8-bit ST and `ESC \\` still terminate strings. CAN and SUB discard an
unfinished OSC command without applying its title or clipboard change.

CI builds with GCC and Clang, runs the X11 fixtures under Xvfb, and repeats the
non-X11 tests with AddressSanitizer and UndefinedBehaviorSanitizer. Leak detection
is disabled in that sanitizer job; these checks do not establish leak freedom.
