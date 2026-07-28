# st parity boundary

cupidterminal is derived from the bundled st checkout at commit
`04ce0d6f06e6552dcbb3a1643a346f5e803cd23f` (st 0.9.3 plus the upstream
zero-row and async-signal-safety fixes).

The bundled source is the reference for PTY lifecycle, escape modes, X11
selection behavior, keyboard mappings, window hints, command-line options,
terminfo, and licensing. Direct ports should preserve that behavior unless
this document records a deliberate extension.

Deliberate Cupid extensions:

- X11-free terminal/parser translation unit enforced by `make check-no-x11`.
- A 2000-line scrollback ring.
- Sparse combining-mark storage alongside the st-shaped base `Glyph`.
- UTF-8 fallback font discovery and emoji rendering.
- OSC 4, 10, 11, 12, 52, 104, 110, 111, and 112 handling.
- Runtime zoom shortcuts and per-row dirty rendering.
- Nonblocking PTY writes with an ordered backpressure queue.

The installed terminal name is `cupidterminal-256color`. Its terminfo source
starts from `st.info`; capabilities are added only when the parser and X11
input/output paths implement them.
