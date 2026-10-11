# Hard reset

RIS (`ESC c`) restores UTF-8 decoding, remote echo, and normal newline handling.
It also clears focus reporting, reverse video, cursor-shape overrides, selection,
and pending tab-overwrite state. These fields previously survived a reset, so an
application could leave later shell input or display behavior changed.

`test/parser/test_hard_reset.c` enables the modes before RIS and checks their
initial values afterward. It then prints an accented UTF-8 character to verify
the decoder's behavior, rather than only checking the mode flag.

The internal reset helper handles RIS. DECSTR (`CSI ! p`) is not implemented;
the feature reference now says so.
