# st parity boundary

cupidterminal is derived from upstream st commit
`04ce0d643ed17793803e8516f4c9a5b13b93c400` (st 0.9.3 plus the upstream
zero-row and async-signal-safety fixes).

That pinned upstream source is the reference for PTY lifecycle, escape modes, X11
selection behavior, keyboard mappings, window hints, command-line options,
terminfo, and licensing. Direct ports should preserve that behavior unless
this document records a deliberate extension.

The source is intentionally not vendored. Reproduce it with:

```sh
git clone https://git.suckless.org/st st-reference
git -C st-reference checkout 04ce0d643ed17793803e8516f4c9a5b13b93c400
```

Deliberate Cupid extensions:

- X11-free terminal/parser translation unit enforced by `make check-no-x11`.
- A 2000-line scrollback ring.
- Sparse combining-mark storage alongside the st-shaped base `Glyph`.
- UTF-8 fallback font discovery and emoji rendering.
- OSC 4, 10, 11, 12, 52, 104, 110, 111, and 112 handling.
- Runtime zoom shortcuts and per-row dirty rendering.
- Nonblocking PTY writes with an ordered backpressure queue.

The default terminal name is `st-256color`, matching upstream st and avoiding
an unknown `TERM` when running directly from the build tree. The bundled
`cupidterminal-256color` terminfo source starts from `st.info`; capabilities
are added only when the parser and X11 input/output paths implement them.
