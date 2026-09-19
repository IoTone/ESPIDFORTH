/*
 * trailcam_words.c — an ESPIDFORTH vocabulary for the XIAO ESP32S3 Sense.
 *
 * Fourteen words covering the camera, the microSD card and timing. Everything
 * above this line is capture *policy*, and policy belongs in Forth where you
 * can change it without a reflash. See policy.fs.
 *
 * Call trailcam_init() then trailcam_register_words() from app_main(), after
 * forth_init() and before forth_repl().
 *
 * Licensed under the Apache License, Version 2.0
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"

#include "forth_core.h"
#include "pins_xiao_s3_sense.h"
#include "trailcam_words.h"

static const char *TAG = "trailcam";

#define SD_MOUNT_POINT "/sdcard"

static sdmmc_card_t *s_card = NULL;

/* The frame held between cam-snap and sd-write.
 *
 * esp32-camera hands out a limited pool of frame buffers (fb_count below) and
 * will block or fail once they are all outstanding, so exactly one frame is
 * held here at a time and cam-snap returns the previous one before grabbing.
 * Forth code must still call cam-release when it decides not to save a frame —
 * see the drop-frame branch in policy.fs. */
static camera_fb_t *s_fb = NULL;

/* ------------------------------------------------------------------ */
/* Hardware bring-up                                                    */
/* ------------------------------------------------------------------ */

static esp_err_t camera_start(void)
{
    camera_config_t cfg = {
        .pin_pwdn     = XIAO_PWDN_GPIO,
        .pin_reset    = XIAO_RESET_GPIO,
        .pin_xclk     = XIAO_XCLK_GPIO,
        .pin_sccb_sda = XIAO_SIOD_GPIO,
        .pin_sccb_scl = XIAO_SIOC_GPIO,
        .pin_d7       = XIAO_Y9_GPIO,
        .pin_d6       = XIAO_Y8_GPIO,
        .pin_d5       = XIAO_Y7_GPIO,
        .pin_d4       = XIAO_Y6_GPIO,
        .pin_d3       = XIAO_Y5_GPIO,
        .pin_d2       = XIAO_Y4_GPIO,
        .pin_d1       = XIAO_Y3_GPIO,
        .pin_d0       = XIAO_Y2_GPIO,
        .pin_vsync    = XIAO_VSYNC_GPIO,
        .pin_href     = XIAO_HREF_GPIO,
        .pin_pclk     = XIAO_PCLK_GPIO,

        .xclk_freq_hz = 20000000,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG,
        .frame_size   = FRAMESIZE_SVGA,   /* 800x600 — change live with cam-size */
        .jpeg_quality = 12,               /* 10 best .. 63 worst */
        .fb_count     = 2,
        .fb_location  = CAMERA_FB_IN_PSRAM,
        .grab_mode    = CAMERA_GRAB_LATEST,
    };
    return esp_camera_init(&cfg);
}

static esp_err_t sd_start_at(int khz)
{
    if (s_card) return ESP_OK;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    /* 20 MHz is the SDSPI default. Long jumper leads or a tired card want
     * 10000 here; the symptom of running too fast is a mount that succeeds
     * and then throws CRC errors on the first large write. */
    host.max_freq_khz = khz;

    spi_bus_config_t bus = {
        .mosi_io_num     = XIAO_SD_MOSI_GPIO,
        .miso_io_num     = XIAO_SD_MISO_GPIO,
        .sclk_io_num     = XIAO_SD_SCLK_GPIO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(host.slot, &bus, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {   /* INVALID_STATE = already up */
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.gpio_cs = XIAO_SD_CS_GPIO;
    dev.host_id = host.slot;

    esp_vfs_fat_mount_config_t mount = {
        .format_if_mount_failed = false,  /* never reformat a card for the user */
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };

    err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &dev, &mount, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount at %d kHz: %s", khz, esp_err_to_name(err));
        s_card = NULL;
    } else {
        ESP_LOGI(TAG, "mounted at %d kHz", khz);
    }
    return err;
}

static esp_err_t sd_start(void) { return sd_start_at(SDMMC_FREQ_DEFAULT); }

/* ( khz -- flag )  Try mounting the card at a given SPI clock. */
static void w_sd_mount(void)
{
    intptr_t khz = forth_pop();
    if (khz < 400) khz = 400;
    forth_push(sd_start_at((int)khz) == ESP_OK ? -1 : 0);
}

/* ( -- )  Dump what the card reports about itself. */
static void w_sd_info(void)
{
    if (!s_card) { printf("no card mounted\n"); return; }
    sdmmc_card_print_info(stdout, s_card);
    fflush(stdout);
}

esp_err_t trailcam_init(void)
{
    esp_err_t cam = camera_start();
    if (cam != ESP_OK) ESP_LOGE(TAG, "camera init: %s", esp_err_to_name(cam));

    esp_err_t sd = sd_start();   /* non-fatal: the REPL is still useful without it */

    return (cam == ESP_OK && sd == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/* ------------------------------------------------------------------ */
/* Camera words                                                         */
/* ------------------------------------------------------------------ */

/* ( -- len )  Capture a JPEG. Pushes its byte length, or 0 on failure.
 *             The frame is held until sd-write or cam-release. */
static void w_cam_snap(void)
{
    if (s_fb) { esp_camera_fb_return(s_fb); s_fb = NULL; }
    s_fb = esp_camera_fb_get();
    forth_push(s_fb ? (intptr_t)s_fb->len : 0);
}

/* ( -- )  Return the held frame to the driver's pool. */
static void w_cam_release(void)
{
    if (s_fb) { esp_camera_fb_return(s_fb); s_fb = NULL; }
}

/* ( n -- )  JPEG quality, 10 (best) .. 63 (worst). Lower = bigger files. */
static void w_cam_quality(void)
{
    intptr_t q = forth_pop();
    if (q < 10) q = 10;
    if (q > 63) q = 63;
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_quality(s, (int)q);
}

/* ( n -- )  Frame size, as a framesize_t ordinal. The useful ones on this
 *           sensor: 5 = QVGA 320x240, 8 = VGA 640x480, 9 = SVGA 800x600,
 *           10 = XGA 1024x768, 13 = UXGA 1600x1200. */
static void w_cam_size(void)
{
    intptr_t fs = forth_pop();
    if (fs < 0) fs = 0;
    if (fs > FRAMESIZE_UXGA) fs = FRAMESIZE_UXGA;
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_framesize(s, (framesize_t)fs);
}

/* ( -- w h )  Dimensions of the held frame, or 0 0 if none. */
static void w_cam_dims(void)
{
    forth_push(s_fb ? (intptr_t)s_fb->width  : 0);
    forth_push(s_fb ? (intptr_t)s_fb->height : 0);
}

/* ( -- )  Base64 the held frame to the console between markers, so a host
 *         can recover the actual JPEG from a board with no display. */
static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void w_cam_dump(void)
{
    if (!s_fb) { printf("\n[no frame held]\n"); return; }
    const uint8_t *b = s_fb->buf;
    size_t n = s_fb->len;
    printf("\n---JPEG-BEGIN %u---\n", (unsigned)n);
    char line[77];
    int col = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)b[i] << 16;
        if (i + 1 < n) v |= (uint32_t)b[i+1] << 8;
        if (i + 2 < n) v |= (uint32_t)b[i+2];
        line[col++] = B64[(v >> 18) & 63];
        line[col++] = B64[(v >> 12) & 63];
        line[col++] = (i + 1 < n) ? B64[(v >> 6) & 63] : '=';
        line[col++] = (i + 2 < n) ? B64[v & 63]        : '=';
        if (col >= 76) { line[col] = 0; printf("%s\n", line); col = 0; }
    }
    if (col) { line[col] = 0; printf("%s\n", line); }
    printf("---JPEG-END---\n");
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* SD card words                                                        */
/* ------------------------------------------------------------------ */

/* ( n -- flag )  Write the held frame to /sdcard/IMG_<n>.JPG.
 *                Pushes -1 on success, 0 on any failure. Does NOT release
 *                the frame — policy decides when to do that. */
static void w_sd_write(void)
{
    intptr_t n = forth_pop();

    if (!s_fb || !s_card) { forth_push(0); return; }

    char path[40];
    snprintf(path, sizeof path, SD_MOUNT_POINT "/IMG_%05ld.JPG", (long)n);

    FILE *f = fopen(path, "wb");
    if (!f) { ESP_LOGW(TAG, "open %s failed", path); forth_push(0); return; }

    size_t wrote = fwrite(s_fb->buf, 1, s_fb->len, f);
    fclose(f);

    if (wrote != s_fb->len) {
        ESP_LOGW(TAG, "short write %s: %u of %u", path,
                 (unsigned)wrote, (unsigned)s_fb->len);
        forth_push(0);
        return;
    }
    forth_push(-1);
}

/* ( -- mb )  Free space on the card in whole megabytes, 0 if unmounted. */
static void w_sd_free(void)
{
    uint64_t total = 0, freeb = 0;
    if (!s_card || esp_vfs_fat_info(SD_MOUNT_POINT, &total, &freeb) != ESP_OK) {
        forth_push(0);
        return;
    }
    forth_push((intptr_t)(freeb / (1024 * 1024)));
}

/* ( -- n )  How many .JPG files are on the card. */
static void w_sd_count(void)
{
    DIR *d = opendir(SD_MOUNT_POINT);
    if (!d) { forth_push(0); return; }

    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *dot = strrchr(e->d_name, '.');
        if (dot && strcasecmp(dot, ".JPG") == 0) n++;
    }
    closedir(d);
    forth_push(n);
}

/* ( -- flag )  Is the card mounted? */
static void w_sd_ok(void)
{
    forth_push(s_card ? -1 : 0);
}

/* ------------------------------------------------------------------ */
/* Timing                                                              */
/* ------------------------------------------------------------------ */

/* ( n -- )  Block this task for n milliseconds. */
static void w_ms(void)
{
    intptr_t n = forth_pop();
    if (n > 0) vTaskDelay(pdMS_TO_TICKS(n));
}

/* ( -- t )  Microseconds since boot. Truncated to a 32-bit cell, so this
 *           wraps about every 71 minutes — fine for timing a word, not a
 *           clock. */
static void w_us(void)
{
    forth_push((intptr_t)esp_timer_get_time());
}

/* ------------------------------------------------------------------ */

void trailcam_register_words(void)
{
    forth_register_word("cam-snap",    w_cam_snap);
    forth_register_word("cam-release", w_cam_release);
    forth_register_word("cam-quality", w_cam_quality);
    forth_register_word("cam-size",    w_cam_size);
    forth_register_word("cam-dims",    w_cam_dims);
    forth_register_word("cam-dump",    w_cam_dump);

    forth_register_word("sd-write",    w_sd_write);
    forth_register_word("sd-free",     w_sd_free);
    forth_register_word("sd-count",    w_sd_count);
    forth_register_word("sd-ok?",      w_sd_ok);
    forth_register_word("sd-mount",    w_sd_mount);
    forth_register_word("sd-info",     w_sd_info);

    forth_register_word("ms",          w_ms);
    forth_register_word("us",          w_us);
}
