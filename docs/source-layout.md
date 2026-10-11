# Source layout

| File | Responsibility |
|---|---|
| `src/cupid.c` | Terminal logic. Parser, screen model, scrollback, selection, tty I/O, `main()`. **Zero Xlib symbols.** |
| `src/cupid.h` | Public types (`Glyph`, `Line`, `Rune`, `Term`, `TermLine`, `CombMark`, `Arg`) and prototypes (`tnew`, `tresize`, `twrite`, `ttywrite`, `treset`, `tputc`, `selstart`, `selextend`, `selclear`, `selected`, `getsel`, `kscrollup_n`, `kscrolldown_n`, `term_render_cluster`, ...). |
| `src/xwin.c` | All Xlib / Xft. Window, fonts, draw cycle, XIM, keymap dispatch, clipboard, mouse-to-selection, event loop (`run`). |
| `src/xwin.h` | Pure callback contract `cupid.c → xwin.c` (`xbell`, `xdrawline`, `xstartdraw`, `xfinishdraw`, `xsettitle`, `xsetsel`, `xresize`, `xsetmode`, ...). |
| `src/xentry.h` | Entry-point declarations called from `main()`: `xinit`, `xsync_pty_winsize`, `run`, `parse_geometry_str`. |
| `src/xconfig.h` | X11-aware config view. Keymap tables, modifier macros. Included only by `xwin.c`. |
| `src/config.h`, `src/config.def.h` | X11-free settings (font, colors, latency, defaults). Included by both `cupid.c` and `xconfig.h`. |
| `src/pty.c`, `src/pty.h` | PTY spawn / read / write / reap. |
| `src/arg.h` | argv parsing (suckless ARG convention). |

The `cupid.c` / `xwin.c` split is enforced by `make check-no-x11`: the build fails if any Xlib symbol leaks into `cupid.c`.

