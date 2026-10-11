# OSC input validation

OSC command numbers and palette indices are checked as decimal fields before
dispatch. Non-numeric suffixes, negative values, and values above `INT_MAX` are
rejected. Previously, narrowing a large number could select a different command
or palette entry; non-numeric command text could also set the title.

OSC 52 rejects misplaced and incomplete base64 padding. Clipboard updates still
require window operations to be enabled. Padded and unpadded valid data remain
supported, including the existing whitespace handling.

`test/parser/test_osc_validation.c` covers malformed commands, overflowing
palette indices, invalid padding, and valid clipboard payloads. The numeric and
base64 cases were each reproduced before their respective fixes.
