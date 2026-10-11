# Newline mode and long input

With LNM enabled, `ttywrite` converts carriage returns to CRLF for all input
lengths. Expansion uses a 512-byte scratch buffer and sends completed chunks in
order. Local echo still processes the original input once. A transport error
stops further writes.

The regression captures calls from the real terminal writer without using a
PTY. It checks lengths 0, 1, 255, 256, 257, 511, 512, and 1025 with LNM on and
off. It runs through `make test-pty`.
