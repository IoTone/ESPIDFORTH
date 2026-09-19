\ 03 - Conditionals and loops

: sign 0 > if ." positive" else ." not positive" then cr ;
5 sign
-3 sign

: countup 10 0 do i . loop cr ;
countup

: evens 20 0 do i . 2 +loop cr ;
evens

: sum-to 0 swap 1 + 1 do i + loop ;
10 sum-to .          \ 55

\ nested loops: i is the inner index, j the outer
: grid 3 0 do 3 0 do i j * . loop cr loop ;
grid

: countdown begin dup . 1 - dup 0= until drop cr ;
5 countdown
