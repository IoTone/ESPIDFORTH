\ 04 - Variables, constants and the dictionary heap

variable counter
0 counter !
: bump counter @ 1 + counter ! ;
bump bump bump
counter @ .          \ 3

100 constant limit
limit .              \ 100

variable total
0 total !
: accumulate 5 0 do i total @ + total ! loop ;
accumulate
total @ .            \ 10

\ here pushes the allocation pointer; allot advances it
here 4 allot here swap - .   \ 4
