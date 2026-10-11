# Wide glyphs during row edits

A double-width glyph occupies a lead cell and a dummy cell. Character insertion,
deletion, insert-mode writes, and window narrowing can cut through that pair.
The affected fragment is cleared so a wide lead always has an adjacent dummy,
and a dummy always has an adjacent lead. Complete pairs keep their text and
combining marks.

`test/screen/test_wide_transforms.c` exercises edits at every column of a row
containing two wide clusters. It also checks surrounding text, combining-mark
cleanup, and primary/alternate-screen resizing. Run it through `make test-screen`.
