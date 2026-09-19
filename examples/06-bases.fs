\ 06 - Number bases, strings and stack surgery

255 hex . decimal    \ ff
0xFF .               \ 255
%1010 .              \ 10
$2A .                \ 42

s" hello world" type cr
65 emit 66 emit 67 emit cr    \ ABC

1 2 3 0 pick .       \ 3   (0 pick is dup)
2drop drop
1 2 3 2 pick .       \ 1
2drop drop
