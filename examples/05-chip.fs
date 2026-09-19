\ 05 - Interrogating the chip (built-in FFI words)

chip-info
chip-cores .
chip-model .
chip-rev .

: board-report
  ." cores: " chip-cores . cr
  ." rev:   " chip-rev . cr
  ." heap:  " free-heap . cr ;
board-report

mac-addr .s          \ two cells: low 4 bytes, high 2
2drop

mem                  \ full memory report
