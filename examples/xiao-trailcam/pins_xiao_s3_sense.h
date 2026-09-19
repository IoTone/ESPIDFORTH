// Seeed Studio XIAO ESP32S3 Sense pin map.
//
// Camera (on the Sense expansion board — OV2640 on older units, OV5640 on
// newer ones; the esp32-camera driver probes and picks. The pin map is the
// same either way. The board tested here reported PID=0x5640.):
//   Source: espressif/esp32-camera component, examples/camera_example/main/
//           camera_pinout.h, #ifdef BOARD_ESP32S3_XIAO — verified against
//           Seeed's pin-multiplexing table, which lists exactly this GPIO set
//           (10,11,12,13,14,15,16,17,18,38,39,40,47,48) as camera pins.
//   No PWDN and no RESET line are wired; the driver does a software reset.
//
// microSD (also on the Sense expansion board, SPI mode):
//   Source: wiki.seeedstudio.com/xiao_esp32s3_pin_multiplexing/ and
//           .../xiao_esp32s3_sense_filesystem/
//
// ---------------------------------------------------------------------------
// GPIO21 IS SHARED. Seeed's own pin table lists GPIO21 as BOTH "USER_LED"
// and "Onboard SD Card__CS". They are the same pin. Consequences:
//
//   * You cannot use the user LED as a status indicator while the SD card is
//     mounted — driving it low asserts chip select on the card.
//   * The LED will flicker visibly during SD writes. That is not a fault.
//   * CS idles HIGH, and the LED is active LOW, so the LED is normally dark
//     and blinks on card access. Treat that as the only LED behaviour you get.
//
// This sample therefore never touches the LED. If you want a status light,
// bring one out on a free pin (D0/GPIO1 through D5/GPIO6 are unused here).
// ---------------------------------------------------------------------------
//
// Also note: D11/D12 on the Sense board are reserved for the PDM microphone,
// which this sample does not use.

#ifndef PINS_XIAO_S3_SENSE_H
#define PINS_XIAO_S3_SENSE_H

/* ---- camera (OV2640 / OV5640) ---- */
#define XIAO_PWDN_GPIO    -1   /* not wired */
#define XIAO_RESET_GPIO   -1   /* not wired — software reset */
#define XIAO_XCLK_GPIO    10
#define XIAO_SIOD_GPIO    40   /* SCCB SDA */
#define XIAO_SIOC_GPIO    39   /* SCCB SCL */
#define XIAO_Y9_GPIO      48   /* D7 */
#define XIAO_Y8_GPIO      11   /* D6 */
#define XIAO_Y7_GPIO      12   /* D5 */
#define XIAO_Y6_GPIO      14   /* D4 */
#define XIAO_Y5_GPIO      16   /* D3 */
#define XIAO_Y4_GPIO      18   /* D2 */
#define XIAO_Y3_GPIO      17   /* D1 */
#define XIAO_Y2_GPIO      15   /* D0 */
#define XIAO_VSYNC_GPIO   38
#define XIAO_HREF_GPIO    47
#define XIAO_PCLK_GPIO    13

/* ---- microSD over SPI ---- */
#define XIAO_SD_SCLK_GPIO  7   /* silkscreen D8  */
#define XIAO_SD_MISO_GPIO  8   /* silkscreen D9  */
#define XIAO_SD_MOSI_GPIO  9   /* silkscreen D10 */
#define XIAO_SD_CS_GPIO   21   /* shared with USER_LED — see note above */

#endif /* PINS_XIAO_S3_SENSE_H */
