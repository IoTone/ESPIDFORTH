\ 01 - The stack and postfix notation
\ Paste a line at a time into the ok> prompt.

2 3 + .              \ 5
10 4 - .             \ 6
20 4 / .             \ 5
17 5 mod .           \ 2

1 2 3 .s             \ <3> 1 2 3
drop .s              \ <2> 1 2
swap .s              \ <2> 2 1
dup .s               \ <3> 2 1 1
2drop 2drop          \ clean up

-7 abs .             \ 7
3 7 min .            \ 3
3 7 max .            \ 7

7 3 + 10 4 - * .     \ (7+3)*(10-4) = 60
