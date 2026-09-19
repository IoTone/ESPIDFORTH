\ 02 - Defining your own words

: square dup * ;
5 square .           \ 25
12 square .          \ 144

: cube dup square * ;
3 cube .             \ 27

\ Integer-safe Fahrenheit to Celsius: (f-32)*10/18
: f>c 32 - 10 * 18 / ;
212 f>c .            \ 100
98 f>c .             \ 36

: greet ." Hello from Forth" cr ;
greet
