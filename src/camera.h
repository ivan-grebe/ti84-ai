#pragma once

#include <Arduino.h>
#include "esp_camera.h"
#include "mbedtls/base64.h"
#include "camera_pins.h"
#include "config.h"

namespace Camera {

static bool initialized = false;
static uint8_t *lastJpeg = nullptr;
static size_t lastJpegLen = 0;
static uint16_t lastJpegWidth = 0;
static uint16_t lastJpegHeight = 0;
static unsigned long lastCaptureMs = 0;

static void clearLastCapture() {
    if (lastJpeg) {
        free(lastJpeg);
        lastJpeg = nullptr;
    }
    lastJpegLen = 0;
    lastJpegWidth = 0;
    lastJpegHeight = 0;
    lastCaptureMs = 0;
}

static bool storeLastCapture(camera_fb_t *fb) {
    if (!fb || !fb->buf || fb->len == 0) return false;

    uint8_t *copy = (uint8_t *)ps_malloc(fb->len);
    if (!copy) {
        copy = (uint8_t *)malloc(fb->len);
    }
    if (!copy) {
        Serial.println("JPEG copy malloc failed");
        return false;
    }

    memcpy(copy, fb->buf, fb->len);
    clearLastCapture();
    lastJpeg = copy;
    lastJpegLen = fb->len;
    lastJpegWidth = fb->width;
    lastJpegHeight = fb->height;
    lastCaptureMs = millis();
    Serial.printf("Stored last JPEG: %u bytes (%ux%u)\n",
                  (unsigned)lastJpegLen, lastJpegWidth, lastJpegHeight);
    return true;
}

bool init() {
    if (initialized) return true;

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = CAM_XCLK_FREQ;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode    = CAMERA_GRAB_LATEST;

    // Use PSRAM for frame buffers if available
    if (psramFound()) {
        config.frame_size   = CAM_FRAMESIZE;
        config.jpeg_quality = CAM_JPEG_QUALITY;
        config.fb_count     = 2;
        config.fb_location  = CAMERA_FB_IN_PSRAM;
    } else {
        config.frame_size   = FRAMESIZE_QVGA;
        config.jpeg_quality = 15;
        config.fb_count     = 1;
        config.fb_location  = CAMERA_FB_IN_DRAM;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        return false;
    }

    // ── Fix upside-down + mirrored camera ──
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 1);     // Flip vertically (installed upside down)
        s->set_hmirror(s, 0);   // Keep text readable left-to-right

        // ── Image quality improvements ──
        s->set_brightness(s, 1);       // Slight brightness boost (-2 to 2)
        s->set_contrast(s, 1);         // Slight contrast boost (-2 to 2)
        s->set_saturation(s, 0);       // Normal saturation
        s->set_whitebal(s, 1);         // Enable auto white balance
        s->set_awb_gain(s, 1);         // Enable AWB gain
        s->set_wb_mode(s, 0);          // Auto WB mode
        s->set_exposure_ctrl(s, 1);    // Enable auto exposure
        s->set_aec2(s, 1);             // Enable AEC DSP
        s->set_ae_level(s, 1);         // Slight exposure boost (-2 to 2)
        s->set_gain_ctrl(s, 1);        // Enable auto gain
        s->set_agc_gain(s, 0);         // AGC gain starting point
        s->set_gainceiling(s, (gainceiling_t)6);  // Max gain ceiling
        s->set_bpc(s, 1);              // Black pixel correction
        s->set_wpc(s, 1);              // White pixel correction
        s->set_lenc(s, 1);             // Lens correction
        s->set_raw_gma(s, 1);          // Gamma correction
    }

    initialized = true;
    Serial.println("Camera initialized (vflip enabled, hmirror disabled)");
    return true;
}

// Capture a JPEG frame. Caller must call esp_camera_fb_return() when done.
camera_fb_t* capture() {
    if (!initialized && !init()) return nullptr;

    // Discard first frame (often has stale auto-exposure)
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);

    // Capture the real frame
    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return nullptr;
    }
    Serial.printf("Captured %u bytes JPEG (%dx%d)\n", fb->len, fb->width, fb->height);
    return fb;
}

// Capture and return as base64-encoded string (using mbedtls)
String captureBase64() {
    camera_fb_t *fb = capture();
    if (!fb) return "";

    storeLastCapture(fb);

    // Calculate base64 output size
    size_t olen = 0;
    mbedtls_base64_encode(NULL, 0, &olen, fb->buf, fb->len);

    // Allocate buffer and encode
    uint8_t *b64buf = (uint8_t *)ps_malloc(olen + 1);
    if (!b64buf) {
        // Fallback to regular malloc if PSRAM unavailable
        b64buf = (uint8_t *)malloc(olen + 1);
    }
    if (!b64buf) {
        Serial.println("Base64 malloc failed");
        esp_camera_fb_return(fb);
        return "";
    }

    mbedtls_base64_encode(b64buf, olen + 1, &olen, fb->buf, fb->len);
    b64buf[olen] = '\0';
    esp_camera_fb_return(fb);

    String result((char *)b64buf);
    free(b64buf);

    Serial.printf("Base64 encoded: %u bytes\n", result.length());
    return result;
}

bool capturePreviewJpeg(uint8_t **outBuf, size_t *outLen) {
    if (!outBuf || !outLen) return false;

    camera_fb_t *fb = capture();
    if (!fb) return false;

    uint8_t *copy = (uint8_t *)ps_malloc(fb->len);
    if (!copy) {
        copy = (uint8_t *)malloc(fb->len);
    }
    if (!copy) {
        Serial.println("Preview JPEG malloc failed");
        esp_camera_fb_return(fb);
        return false;
    }

    memcpy(copy, fb->buf, fb->len);
    *outBuf = copy;
    *outLen = fb->len;
    esp_camera_fb_return(fb);
    return true;
}

const uint8_t *lastCaptureData() { return lastJpeg; }
size_t lastCaptureSize() { return lastJpegLen; }
uint16_t lastCaptureWidth() { return lastJpegWidth; }
uint16_t lastCaptureHeight() { return lastJpegHeight; }
unsigned long lastCaptureTime() { return lastCaptureMs; }
bool hasLastCapture() { return lastJpeg != nullptr && lastJpegLen > 0; }

}  // namespace Camera
