# Selection resizing and large input

Resizing clears an active text selection when the row or column count changes.
Keeping the old endpoints allowed word snapping to read beyond a newly shortened
row. A notification with unchanged dimensions preserves the selection.

The public `twrite` entry point accepts input larger than one PTY read. It feeds
the parser in chunks of at most 65,536 bytes, retaining incomplete sequences
between chunks. Previously, merging a pending escape with a large write silently
truncated the supplied bytes to the fixed merge-buffer size.

`test/screen/test_resize_selection.c` covers selection types, snapping, unchanged
dimensions, and smaller screens. `test/parser/test_large_write.c` checks sizes
around the parser buffer boundaries and a 150,000-byte write, with and without
pending escapes. Both run through `make test-all` and sanitizer CI.
