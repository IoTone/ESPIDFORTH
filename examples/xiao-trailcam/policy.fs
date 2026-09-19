\ ============================================================
\ trailcam — capture policy
\
\ This is the half of the project you edit while it runs. The C
\ vocabulary (trailcam_words.c) never changes; everything below is
\ typed at the ok> prompt or sent as a role bundle.
\
\ Requires: cam-snap cam-release cam-size cam-quality
\           sd-write sd-free sd-count sd-ok? sd-mount  ms us
\
\ Verified on a XIAO ESP32S3 Sense (OV5640), 2026-09-19.
\ ============================================================

variable shot#
variable saved
variable dropped
variable base       \ running baseline of quiet-scene JPEG size
variable margin     \ percent above baseline that counts as motion

0 shot# !
0 saved !
0 dropped !
10 margin !

\ The engine has no +! , so increments are spelled out.
: bump      shot# @   1 + shot# ! ;
: +saved    saved @   1 + saved ! ;
: +dropped  dropped @ 1 + dropped ! ;


\ ------------------------------------------------------------
\ Why a running baseline and not a fixed threshold
\ ------------------------------------------------------------
\ A JPEG of a static scene compresses small; something entering the
\ frame adds detail and the encoded size rises. That part is true and
\ it is free — the encoder already did the work.
\
\ What is NOT true is that the quiet level holds still. Measured on
\ real hardware, the same motionless scene sat in a 0.5% band one
\ minute and a 7.8% band a few minutes later, drifting upward as the
\ sensor's auto-exposure and gain moved. A floor chosen once fired on
\ every frame within minutes.
\
\ So the baseline has to follow the scene. This is an integer moving
\ average with roughly an 8-frame time constant:
\
\     base <- base - base/8 + new/8

: learn  ( n -- )
  base @ dup 8 / - swap 8 / + base ! ;

\ Seed the baseline before trusting it.
: prime  ( n -- )
  cam-snap base ! cam-release
  0 do cam-snap learn cam-release 100 ms loop ;

\ Motion = this frame is more than margin% above the running baseline.
\ Compared as n*100 > base*(100+margin) to stay in integers; at ~25 KB
\ frames both sides are around 2.5e6, well inside a 32-bit cell.
: motion?  ( n -- n flag )
  dup 100 *  base @ 100 margin @ + *  > ;


\ ------------------------------------------------------------
\ Capture
\ ------------------------------------------------------------

: save-frame
  shot# @ sd-write drop
  bump +saved ;

\ One observation: look, decide, release.
\
\ Note what this does NOT do: learn while triggered. Measured on hardware,
\ a hand covering ~40% of the frame is only +14.2% over an empty scene, and
\ the moving average will absorb that in about eight frames. Learning during
\ a detection would make a subject that stays in frame become the new normal
\ within a couple of seconds — the camera would notice an arrival and then go
\ blind to it. Learn only on quiet frames and the baseline tracks the scene
\ without tracking the intruder.
: step
  cam-snap
  motion?
  if   save-frame drop
  else +dropped   learn
  then
  cam-release ;

: watch  ( n -- )
  0 do step 150 ms loop ;


\ ------------------------------------------------------------
\ Guarding the card
\ ------------------------------------------------------------
\ Note: this engine has no LEAVE, so the loop cannot exit early. It
\ runs its full count and simply stops capturing once the card is low.

: room?  ( -- flag )  sd-free 20 > ;

: guarded  ( n -- )
  0 do
    room? if step then
    150 ms
  loop ;


\ ------------------------------------------------------------
\ Looking at what the camera is actually doing
\ ------------------------------------------------------------

\ Raw sizes — run this first, on the scene you care about, and watch
\ how wide the quiet band really is before you trust any threshold.
: sizes  ( n -- )
  0 do cam-snap . cam-release 300 ms loop cr ;

\ Live trigger preview: '!' fires, '.' is quiet. Writes nothing.
\ Same learn-only-when-quiet rule as step.
: preview  ( n -- )
  0 do
    cam-snap
    motion?
    if   33 emit drop
    else 46 emit learn
    then
    cam-release
    150 ms
  loop cr ;

: status
  ." base "    base @ .
  ." margin "  margin @ .
  ." saved "   saved @ .
  ." dropped " dropped @ .
  ." freeMB "  sd-free .
  ." files "   sd-count . cr ;

: elapsed  ( t0 -- dt )  us swap - ;
: bench    us cam-snap drop cam-release elapsed . ."  us/frame" cr ;


\ ------------------------------------------------------------
\ Typical session
\ ------------------------------------------------------------
\   sd-ok? .          \ -1 means the card mounted
\   20 sizes          \ how noisy is this scene, really?
\   10 prime          \ seed the baseline
\   40 preview        \ tune margin until quiet frames stay quiet
\   8 margin !        \ more sensitive
\   bench             \ what does one capture cost?
\   200 guarded       \ run it
\   status
