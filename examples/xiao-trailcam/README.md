# trailcam — a Forth-scriptable capture rig

**Board:** Seeed Studio XIAO ESP32S3 **Sense** — camera + microSD + 8 MB PSRAM.
Tested on hardware 2026-09-19 (ESP32-S3 rev v0.2, OV5640, ESP-IDF 5.3.1).

A camera that writes JPEGs to an SD card, where *when to press the shutter*
lives in Forth and can be rewritten while the camera is running.

The C half is 14 words and never changes. The policy half is a page of Forth
you retype at the `ok>` prompt — or send over the air as a role bundle — until
the camera does what you actually wanted.

---

## Why this board is a good demonstration

Capture policy is the part of a trail camera you get wrong. How long between
frames, what counts as motion worth keeping, what to do when the card fills.
Every one of those is a number you discover by watching the thing run in the
place it will live, and every one of them normally costs an edit-build-flash
cycle with the camera unmounted and on your bench.

Here they cost a line of typing with the camera still pointed at the scene.
The adaptive trigger in `policy.fs` was in fact *designed that way* — see
"What the hardware changed" below.

## Files

| File | What it is |
|------|------------|
| `trailcam_words.c` | The FFI vocabulary — camera, SD, timing. Flash once. |
| `trailcam_words.h` | Two entry points and the word list. |
| `pins_xiao_s3_sense.h` | Pin map, with sources and the GPIO21 warning. |
| `policy.fs` | The Forth layer. This is the part you edit. |
| `platformio-env.ini` | Build environment to append to `platformio.ini`. |
| `sdkconfig.defaults.xiao_s3_sense` | Flash size, PSRAM-BSS, console, FATFS. |
| `partitions.csv` | 3 MB app — the camera driver needs the room. |
| `idf_component.yml` | Pulls `espressif/esp32-camera` (`^2.0.0`). |

## Build

Because the camera and SD code would break ESPIDFORTH's other five build
environments, build this as its own project with `components/forth` copied in:

```
trailcam/
  CMakeLists.txt          # standard IDF project file, project(trailcam)
  platformio.ini          # from platformio-env.ini
  partitions.csv
  sdkconfig.defaults      # from sdkconfig.defaults.xiao_s3_sense
  components/forth/       # copied from ESPIDFORTH
  src/
    CMakeLists.txt        # REQUIRES forth esp_timer driver esp_driver_sdspi fatfs
    main.c                # ESPIDFORTH's src/main.c, plus the two calls below
    trailcam_words.c/.h
    pins_xiao_s3_sense.h
    idf_component.yml
```

In `app_main()`, between `forth_init()` and `forth_repl()`:

```c
forth_init(heap_size);
trailcam_init();             /* camera + SD bring-up */
trailcam_register_words();   /* the 14 words */
forth_repl(console_getchar, console_putchar);
```

Then `pio run -e xiao_s3_sense -t upload -t monitor`.

Verified with **esp32-camera 2.1.7** and **esp_jpeg 1.3.1** (pulled
transitively) under ESP-IDF 5.3.1.

Measured on the real board: **394 KB flash** (12.5% of a 3 MB app partition)
and **27.8 KB internal RAM** — low because the Forth dictionary and code arrays
land in PSRAM via `EXT_RAM_BSS_ATTR`.

Format the microSD as **FAT32** first. See the SD note in the gotchas.

## The vocabulary

```
cam-snap     ( -- len )     capture a JPEG, hold it, push byte length
cam-release  ( -- )         return the held frame to the driver pool
cam-quality  ( n -- )       10 (best) .. 63 (worst)
cam-size     ( n -- )       5=QVGA 8=VGA 9=SVGA 10=XGA 13=UXGA
cam-dims     ( -- w h )     dimensions of the held frame
cam-dump     ( -- )         base64 the held frame to the console
sd-write     ( n -- flag )  write held frame to /sdcard/IMG_<n>.JPG
sd-free      ( -- mb )      free megabytes
sd-count     ( -- n )       .JPG files on the card
sd-ok?       ( -- flag )    is the card mounted
sd-mount     ( khz -- flag ) try mounting at a given SPI clock
sd-info      ( -- )         dump what the card reports about itself
ms           ( n -- )       delay
us           ( -- t )       microseconds since boot
```

`cam-snap` holds the frame until you write or release it. Policy decides
which — that is why `sd-write` does not release on your behalf.

Note `cam-dims` pushes width then height, so `cam-dims . .` prints **height
first**. Use `cam-dims swap . .` if you want them the other way round.

## What the hardware changed

The first version of this sample used a fixed threshold: measure the quiet
scene once, pick a number above it, save anything bigger. On the bench that
looked fine. On the board it fell apart within minutes.

Two measurements of the same motionless scene, taken a few minutes apart:

| | Range (bytes) | Spread |
|---|---|---|
| First | 21296 – 21404 | 0.5% |
| Later | 21395 – 23086 | **7.8%** |

The sensor's auto-exposure and gain drift, and the baseline drifts with them.
A floor set at 22000 — comfortably above the first band — fired on **10 out of
10** frames once the scene had drifted up under it.

So the baseline has to follow the scene. `policy.fs` now keeps an integer
moving average with about an 8-frame time constant and triggers on a
*percentage above the running baseline*:

```forth
: learn  ( n -- )  base @ dup 8 / - swap 8 / + base ! ;
: motion?  ( n -- n flag )  dup 100 * base @ 100 margin @ + * > ;
```

Measured on the board with `margin` at 10%:

| Condition | Result |
|---|---|
| 40 frames, stable scene | 40 quiet, **0 false triggers** |
| Step change (quality 12→8, bigger frames) | fires on the first frame, then re-adapts |
| Baseline after the step | relearned 23195 → 25501 |
| Change back down | no trigger — the test is one-sided by design |

The whole redesign happened at the `ok>` prompt against the live camera,
without a single reflash. That is the argument for this project, and it is
also how the bug was found.

## A session

```
ok> 15 sizes
21296 21329 21339 21302 21332 21404 21371 21387 21370 21382 21399 21382 21392 21346 21356

ok> 10 prime
ok> base @ .
21410
ok> 40 preview
........................................
ok> bench
872  us/frame
```

`sizes` first, always — it tells you how noisy *your* scene is before you trust
any threshold. `preview` runs the trigger without writing anything, so you can
tune `margin` while watching.

### The write dominates everything

Measured on the board, per frame:

| Operation | Cost |
|---|---|
| `cam-snap` alone | **872 µs** |
| `cam-snap` + `sd-write` | **140–202 ms** (mean ~160 ms) |

The SD write is roughly 180× the capture. That caps sustained capture at about
six frames per second, and it means the `150 ms` delay in `watch` and `guarded`
is comparable to a single write — a triggered burst runs at roughly half the
interval you set. Budget for the write, not the capture.

## How well does JPEG size actually detect motion?

Well enough to be useful, and less well than the idea promises. Measured on the
board, with the frames recovered via `cam-dump` and eyeballed to confirm what
was in them:

| Scene | Frame size |
|---|---|
| Empty (ceiling, wall, one rail) | **16777** median, 0.4% spread over 20 frames |
| Hand covering ~40% of the frame | **19166** — **+14.2%** |

So the usable `margin` window for this scene is roughly **1% to 14%**. Feeding
both real values through the real predicate on the device: 16733 with
`margin` 10 gives `0`, 19166 gives `-1`, and 19166 with `margin` 15 gives `0`.

That window is narrower than it looks, because the size signal depends on
*texture*, not area. A smooth, evenly-lit hand against a plain wall barely
changes the encoding — it swaps one low-detail region for another. A textured
subject against a flat background gives a much bigger margin; a flat subject
against a busy background can make the frame **smaller**, which this one-sided
test will never see.

Run `sizes` on your actual scene with your actual subject before trusting any
number here.

### Learn only on quiet frames

The first version of the moving average learned on every frame. With an
8-frame time constant, a subject that enters and *stays* becomes the new
baseline in about three seconds — the camera notices the arrival and then goes
blind to it. `step` and `preview` now call `learn` only in the else branch, so
the baseline follows the scene without following the intruder.

## Known gotchas

**GPIO21 is both the user LED and the SD chip select.** Seeed's own pin table
lists it under both names. You cannot use the LED as a status light while the
card is mounted; it will flicker on every write, and that is normal. This
sample never touches it. For a status light, use a free pin (D0–D5).

**FATFS defaults to 4096-byte sectors; SD cards use 512.** ESP-IDF's default
`CONFIG_FATFS_SECTOR_4096` is sized for wear-levelling on internal flash, and
it compiles FATFS with `FF_MIN_SS == FF_MAX_SS == 4096`, which makes every SD
card fail to mount with `FR_NO_FILESYSTEM (13)`. `sdkconfig.defaults` here sets
`CONFIG_FATFS_SECTOR_512=y`. Do not remove it.

**D11 and D12 are reserved for the microphone** on the Sense expansion board.
Not used here, but do not reach for them.

**No `LEAVE`.** The engine cannot exit a `do` loop early, so `guarded` runs its
full count and merely stops writing once the card is low.

**No `+!`.** Increments are spelled `x @ 1 + x !`.

**`us` wraps roughly every 71 minutes** — a cell is 32 bits and
`esp_timer_get_time()` is 64. Fine for timing a word, useless as a clock.

**`!` takes any address you give it.** A mistyped line like `12000 myvar !`
where `myvar` does not exist pushes 12000, fails to push an address, and then
writes to address 12000. On a host that segfaults; on the device it corrupts
whatever is there. Check `.s` after any `?` error.

**`shot#` restarts at 0 every time you reload `policy.fs`, and `sd-write`
overwrites silently.** Frames are named `IMG_<shot#>.JPG`, so a reload will
start overwriting the images already on the card. Before a real deployment,
seed the counter past what is there — `sd-count shot# !` is the cheap version,
or read the highest existing index if there are gaps.

**`sd-free` reports whole megabytes**, so it will not visibly move until you
have written about fifty frames.

**If the card mounts and then throws CRC errors** on a large write, try a
slower clock — `10000 sd-mount .` — rather than reflashing.

## Verification status

Everything here except the SD write path has been run on real hardware:

- Engine self-test on the board: **58 passed, 0 failed** (20.3 ms).
- FFI suite: **8 passed, 0 failed**, MAC matching the bootloader's.
- Camera: `cam-snap` returns ~21.4 KB at 800×600, `872 us` per capture.
- `policy.fs` loads over the serial link with **zero errors**, 98 dictionary
  entries, and runs against the live sensor.
- Adaptive trigger: 0 false positives in 40 frames; fires on a step change.
- microSD: mounts at 20 MHz. Card reports `SD02G, SDSC, 1889 MB,
  sector_size=512`. `sd-write` returns success, `sd-count` and `sd-free` track
  correctly, and JPEGs land on the card.
- Full `policy.fs` pipeline run end to end: forced capture wrote 12 frames;
  the adaptive trigger then dropped 20 consecutive frames on a static scene
  without writing anything.

- Real motion: a hand in frame measures **+14.2%** against a 0.4% noise floor,
  and the predicate fires on that value at `margin` 10. Both scenes were
  recovered with `cam-dump` and visually confirmed.
- `cam-dump`: 19166-byte frame recovered over serial, decoded, valid JFIF
  800×600 with correct SOI/EOI markers.

**Caveat on that motion figure:** it comes from two stills — one with a hand,
one without — plus the predicate evaluated on both real values, rather than
from a single continuous run that caught a hand crossing the lens. Four live
windows were run and none happened to contain a hand, so the sustained-trigger
behaviour of the learn-freeze fix is reasoned from the numbers, not observed.
