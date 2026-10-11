# Features

### Architecture

- **Hard module boundary.** Terminal logic lives in `cupid.c` and contains zero Xlib symbols. The X11 layer lives in `xwin.c` and talks to the parser only through the callback contract in `xwin.h`. A `make check-no-x11` build gate enforces the boundary by checking for Xlib namespace patterns.
- **st-compatible cell layout.** `Glyph` is exactly 16 bytes (`Rune u`, `uint16_t mode`, `uint32_t fg`, `uint32_t bg`), the same shape upstream st uses. CSI / SGR patches transfer with minimal massaging.
- **Side-channel for combining marks.** Base codepoint lives in `Glyph.u`. Combining marks are stored sparsely on the row (`TermLine.combs`) and re-encoded at render time. Whole-line scrolling preserves the row-owned data.

### VT / ANSI

- **Cursor movement.** CUU / CUD / CUF / CUB, CUP / HVP, CHA / VPA, CNL / CPL, save / restore (DECSC / DECRC), tab forward / back (HT / CBT).
- **Erasing.** CSI K (EL: erase in line, modes 0/1/2), CSI J (ED: erase in display, modes 0/1/2/3 including scrollback clear).
- **Scrolling region.** DECSTBM. Scroll up / down (SU / SD).
- **Insert / delete.** ICH (insert chars), DCH (delete chars), IL (insert lines), DL (delete lines), ECH (erase chars).
- **Pending-wrap semantics.** Right-margin behavior matches st: writing in the rightmost column enters a "pending wrap" state where the next action decides whether wrap occurs.
- **Auto-wrap toggle.** DECAWM (`?7`).
- **Origin mode.** DECOM (`?6`) restricts cursor positioning to the scrolling region.
- **Insert mode.** IRM (mode 4) shifts existing cells right on each character.
- **Repeat.** REP (CSI b) re-emits the last printable.
- **Reverse video.** DECSCNM (`?5`) flips foreground and background screen-wide.
- **Cursor visibility / shape.** DECTCEM (`?25`) toggles, DECSCUSR sets shape (block, underline, bar, snowman) and steady / blinking variant.
- **Application cursor keys.** DECCKM (`?1`) switches arrow keys to SS3 sequences.
- **Charset.** G0-G3 designation, SO / SI and LS2 / LS3 shifts. DEC Special Graphics (VT100 ACS box-drawing).
- **Device attributes / status reports.** DA (CSI c), DSR cursor position (CSI 6n), DECID.
- **Hard reset.** RIS (ESC c) restores terminal modes and clears the live screens. DECSTR (CSI ! p) is not implemented.
- **Status line.** BEL (`\a`) routes to `xbell()` for one ring per character.

### Color

- **Truecolor SGR.** `\033[38;2;r;g;bm` and `\033[48;2;r;g;bm` for direct 24-bit RGB on foreground and background, packed via the `TRUECOLOR(r,g,b)` macro and detected by `IS_TRUECOL(x)`.
- **256-color palette.** `\033[38;5;Nm` and `\033[48;5;Nm` for the standard xterm 256 palette.
- **OSC 4 palette overrides.** `\033]4;index;color\007` redefines individual palette entries at runtime.
- **OSC 10 / 11 / 12 dynamic defaults.** Set the default foreground, background, and cursor colors from the running shell or app.
- **OSC 104 reset.** `\033]104\007` resets palette entries (single index or all).
- **SGR attributes.** Bold, faint, italic, underline, blink, reverse, struck, invisible. Reset codes (22, 23, 24, 25, 27, 29) clear the corresponding bits.

### Unicode

- **UTF-8 input.** Input from PTY, output via Xft. Multi-byte sequences split across reads are buffered and resumed across calls (no partial cluster lost at a read boundary).
- **Wide glyphs.** CJK and emoji occupy a lead cell (`ATTR_WIDE`) and a trailing dummy cell (`ATTR_WDUMMY`); cursor advances by 2; selection across wide cells preserves the full glyph.
- **Combining marks.** Base + diacritics render as one cluster in one cell. Multiple combining marks chain. Selection and scrollback rendering round-trip the full cluster through the visual-line cluster API.

### Selection and clipboard

- **Selection modes.** Regular (line-aware), rectangular (`Alt+drag`), word-snap (double-click), line-snap (triple-click).
- **Public selection API.** `selstart`, `selextend`, `selclear`, `selected`, `getsel` live in `cupid.c`. The X11 layer never mutates selection state directly.
- **X CLIPBOARD.** Copy via `Ctrl+Shift+C` and X CLIPBOARD selection.
- **Primary selection.** Mouse-select fills primary; `Shift+Insert` or middle-click pastes from it.
- **OSC 52 remote copy.** When `allowwindowops` is enabled, `\033]52;c;<base64>\007` from inside ssh, tmux, or vim writes to the local X CLIPBOARD. The st-compatible secure default leaves remote window operations disabled.
- **Bracketed paste.** DECSET `?2004` makes pasted content arrive wrapped in `\033[200~` ... `\033[201~` so editors can disable autoindent on paste.

### Scrollback

- **2000-line history ring buffer.** Lines pushed out of the live screen are retained until the buffer wraps.
- **Scrollback offset.** Per-screen offset; live bottom is offset 0. Visual rows are resolved at render time via `tgetline()` so the renderer is unaware of the live / history split.
- **Input resets scrollback.** Typing snaps back to the live bottom. Incoming PTY output preserves the current history viewport so output does not pull you away from what you are reading.

### Mouse

- **Reporting protocols.** X10 button-only, basic press / release (`?1000`), button-event motion-while-pressed (`?1002`), any-motion (`?1003`).
- **SGR encoding.** `?1006` switches reports to the unbounded `\033[<b;x;y;M/m` format used by modern apps.
- **Application bypass.** `forcemousemod` (default `Shift`) lets you override an app's mouse grab to do a normal text selection.

### Window integration

- **Window title (OSC 0 / 2).** `\033]0;text\007` and `\033]2;text\007` set the X11 window and icon name, routed through `xsettitle()` / `xseticontitle()`.
- **Focus events.** DECSET `?1004` sends `\033[I` and `\033[O` on focus in / out so editors and tmux see when the window has focus.
- **XIM / XIC support.** Compose-key sequences and IME work via X Input Method.
- **Resize.** ConfigureNotify drives the resize path; the parser is told via `tresize()` and the PTY via `ttyresize()`.

### Rendering

- **Xft + fontconfig.** Antialiased text with autohint. Falls back across the user font, a bold variant, and an emoji font.
- **Per-row dirty tracking.** Only changed rows are redrawn each frame.
- **Latency batching.** Adjacent draws are coalesced. `minlatency` (default 2 ms) and `maxlatency` (default 33 ms) bound when an idle redraw fires; this keeps fast PTY output (btop, log streams) from tearing.
- **Cursor shapes.** Block, underline, bar, plus the optional snowman from DECSCUSR.
- **Zoom.** Live font-size adjustment via `Ctrl+Shift+PgUp` / `PgDn`, reset via `Ctrl+Shift+Home`. Window resizes to fit the new cell metrics.

### PTY

- **Non-blocking master fd.** Reads drain to `EAGAIN`; writes that hit backpressure are retained in an ordered queue and flushed when the PTY becomes writable.
- **Echo mode.** LNM (mode 20) and SRM (mode 12) honored when set; `ttywrite(s, n, may_echo)` decides whether to also feed the bytes back to the local screen.
- **Clean child reap.** SIGCHLD handler flags pending; reap loop drains all exited children. The terminal exits cleanly on PTY EOF.

