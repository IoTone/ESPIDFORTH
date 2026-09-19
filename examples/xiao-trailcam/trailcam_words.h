/*
 * trailcam_words.h — XIAO ESP32S3 Sense camera + microSD vocabulary
 * for ESPIDFORTH.
 *
 * Usage from app_main(), after forth_init() and before forth_repl():
 *
 *     forth_init(heap_size);
 *     trailcam_init();               // camera + SD bring-up
 *     trailcam_register_words();     // adds the 11 words below
 *     forth_repl(getchar_fn, putchar_fn);
 *
 * Vocabulary:
 *   cam-snap     ( -- len )     capture a JPEG, hold it, push byte length
 *   cam-release  ( -- )         return the held frame to the driver pool
 *   cam-quality  ( n -- )       JPEG quality 10 (best) .. 63 (worst)
 *   cam-size     ( n -- )       framesize ordinal; 5=QVGA 8=VGA 9=SVGA 13=UXGA
 *   cam-dims     ( -- w h )     dimensions of the held frame
 *   cam-dump     ( -- )         base64 the held frame to the console
 *   sd-write     ( n -- flag )  write held frame to /sdcard/IMG_<n>.JPG
 *   sd-free      ( -- mb )      free megabytes on the card
 *   sd-count     ( -- n )       number of .JPG files on the card
 *   sd-ok?       ( -- flag )    is the card mounted
 *   sd-mount     ( khz -- flag ) try mounting at a given SPI clock
 *   sd-info      ( -- )         dump what the card reports about itself
 *   ms           ( n -- )       delay
 *   us           ( -- t )       microseconds since boot (32-bit, wraps ~71 min)
 *
 * Licensed under the Apache License, Version 2.0
 */

#ifndef TRAILCAM_WORDS_H
#define TRAILCAM_WORDS_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the camera (OV2640 or OV5640 — the driver detects which) and
 * mount the microSD card.
 * Returns ESP_OK only if both succeeded; the REPL is still usable otherwise,
 * and sd-ok? reports the card state. */
esp_err_t trailcam_init(void);

/* Register the vocabulary. Call after forth_init(). */
void trailcam_register_words(void);

#ifdef __cplusplus
}
#endif
#endif /* TRAILCAM_WORDS_H */
