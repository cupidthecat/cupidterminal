# Printer command counts

The input-mirroring scanner bounds decimal accumulation at `INT_MAX`, matching
the CSI parser. Previously a long parameter could overflow signed arithmetic;
`CSI 4294967301 i` could wrap to printer-enable value 5 in the scanner even though
the main parser rejected it. Bytes after that command were then mirrored to the
printer output unexpectedly.

`test/parser/test_printer_counts.c` checks that large parameters do not enable
mirroring and that ordinary enable/disable commands still work. The original
code failed both the output check and a UBSan run with `halt_on_error=1`.
