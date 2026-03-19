# Test Layout

This tree is split by responsibility so parser/screen/utf8/pty behavior can evolve independently.

- [test/common](test/common): shared fixture helpers and assertions.
- [test/parser](test/parser): ANSI/control parser behavior tests.
- [test/screen](test/screen): screen model tests (wrap/scroll/snapshot/cursor restore).
- [test/utf8](test/utf8): UTF-8 byte-stream and multibyte cell behavior tests.
- [test/pty](test/pty): PTY/session integration checks.
- [test/manual](test/manual): manual smoke scripts for visual and interactive checks.

Run automated suites via `make test` or per-suite targets in [Makefile](../Makefile).
