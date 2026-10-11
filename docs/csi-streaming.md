# CSI streaming and dispatch

CSI command prefixes and intermediate bytes are significant. The parser accepts
ordinary numeric parameters, DEC's `?` prefix, colon-separated SGR parameters,
and the space intermediate in DECSCUSR. Unsupported forms are consumed without
falling through to an ordinary command with the same final byte. For example,
`CSI > 4 ; 2 m` must not change display attributes, and `CSI 2 $ J` must not erase
the screen.

The [xterm control-sequence reference](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)
describes the command forms and embedded single-byte controls. This change does
not implement the unsupported commands. Existing bare `CSI q` compatibility is
unchanged.

Both 7-bit and 8-bit CSI use one scanner. Embedded C0 controls execute when they
arrive, including line movement and SI/SO character-set selection. The saved
prefix excludes those controls, so resuming a fragmented command does not
execute them again. NUL and DEL are ignored; CAN and SUB cancel the command,
and ESC starts a fresh escape sequence.

The scanner retains at most 1,024 normalized bytes, including the two-byte CSI
introducer. Commands exceeding that capacity are discarded through their final
byte. This policy applies to complete and fragmented input alike. Embedded
controls and cancellation still work while discarding. The limit is an
implementation bound, not a protocol-defined maximum.

The parser regressions cover unsupported command forms at every split point,
controls before the final byte, oversized commands around the capacity, bytewise
input, and cancellation/reset recovery. They run through `make test-parser` and
are included in CI's normal and sanitizer runs.
