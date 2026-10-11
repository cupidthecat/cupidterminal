# Input recovery

Malformed UTF-8 prefixes are discarded without consuming the valid byte that
follows them. The parser keeps its existing policy of omitting malformed input;
it does not add replacement glyphs. Tests cover every read boundary and a new
multibyte character beginning immediately after an incomplete prefix.

`ESC % @` selects legacy single-byte text, mapping printable bytes 0xa0 through
0xff to their Unicode codepoints. `ESC % G` returns to UTF-8. C1 control handling
is unchanged.

CAN and SUB abandon an unfinished CSI. ESC abandons it and starts a new escape
sequence. This applies to both seven-bit and eight-bit CSI and to sequences
buffered across reads. The cancellation tests assert that an abandoned erase
command cannot erase the screen.

Run these regressions with `make test-parser test-utf8`.

## Child standard streams

PTY startup clears close-on-exec on stdin, stdout, and stderr after duplicating
the slave descriptor. This also covers a slave that was already assigned one of
those numbers, where `dup2(fd, fd)` leaves descriptor flags unchanged.
`make test-pty` exercises all eight combinations of initially closed standard
streams and checks child input, both output streams, and exit status.
