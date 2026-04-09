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
static framesize_t configuredFrameSize = CAM_FRAMESIZE;
static int configuredJpegQuality = CAM_JPEG_QUALITY;
static uint8_t configuredProfile = CAM_PROFILE_DEFAULT;

static uint8_t *allocBuffer(size_t len) {
    uint8_t *buffer = static_cast<uint8_t *>(ps_malloc(len));
    return buffer ? buffer : static_cast<uint8_t *>(malloc(len));
}

static uint8_t resolveQualityProfile(uint8_t profile, framesize_t *frameSize, int *jpegQuality) {
    switch (normalizeCameraProfileValue(profile)) {
        case CAM_PROFILE_LOW:
            *frameSize = FRAMESIZE_VGA;
            *jpegQuality = 10;
            return CAM_PROFILE_LOW;
        case CAM_PROFILE_HIGH:
            *frameSize = FRAMESIZE_UXGA;
            *jpegQuality = 6;
            return CAM_PROFILE_HIGH;
        case CAM_PROFILE_BALANCED:
        default:
            *frameSize = CAM_FRAMESIZE;
            *jpegQuality = CAM_JPEG_QUALITY;
            return CAM_PROFILE_BALANCED;
    }
}

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

    uint8_t *copy = allocBuffer(fb->len);
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

    if (psramFound()) {
        config.frame_size   = configuredFrameSize;
        config.jpeg_quality = configuredJpegQuality;
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

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 1);
        s->set_hmirror(s, 0);

        s->set_brightness(s, 1);
        s->set_contrast(s, 1);
        s->set_saturation(s, 0);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_wb_mode(s, 0);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 1);
        s->set_ae_level(s, 1);
        s->set_gain_ctrl(s, 1);
        s->set_agc_gain(s, 0);
        s->set_gainceiling(s, (gainceiling_t)6);
        s->set_bpc(s, 1);
        s->set_wpc(s, 1);
        s->set_lenc(s, 1);
        s->set_raw_gma(s, 1);
    }

    initialized = true;
    Serial.println("Camera initialized (vflip enabled, hmirror disabled)");
    return true;
}

bool applyQualityProfile(uint8_t profile) {
    framesize_t frameSize = CAM_FRAMESIZE;
    int jpegQuality = CAM_JPEG_QUALITY;
    const uint8_t resolvedProfile = resolveQualityProfile(profile, &frameSize, &jpegQuality);

    if (!psramFound()) {
        configuredFrameSize = FRAMESIZE_QVGA;
        configuredJpegQuality = 15;
        configuredProfile = CAM_PROFILE_LOW;
        Serial.println("Camera quality preset limited by missing PSRAM");
        return true;
    }

    configuredProfile = resolvedProfile;
    configuredFrameSize = frameSize;
    configuredJpegQuality = jpegQuality;

    if (!initialized) {
        Serial.printf("Queued camera preset %u (%d, q=%d)\n",
                      configuredProfile,
                      static_cast<int>(configuredFrameSize),
                      configuredJpegQuality);
        return true;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        Serial.println("Camera sensor unavailable for preset apply");
        return false;
    }

    if (s->set_framesize(s, configuredFrameSize) != 0) {
        Serial.println("Camera set_framesize failed");
        return false;
    }
    if (s->set_quality(s, configuredJpegQuality) != 0) {
        Serial.println("Camera set_quality failed");
        return false;
    }

    Serial.printf("Applied camera preset %u (%d, q=%d)\n",
                  configuredProfile,
                  static_cast<int>(configuredFrameSize),
                  configuredJpegQuality);
    return true;
}

camera_fb_t* capture() {
    if (!initialized && !init()) return nullptr;

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);

    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return nullptr;
    }
    Serial.printf("Captured %u bytes JPEG (%dx%d)\n", fb->len, fb->width, fb->height);
    return fb;
}

String captureBase64() {
    camera_fb_t *fb = capture();
    if (!fb) return "";

    storeLastCapture(fb);

    size_t olen = 0;
    mbedtls_base64_encode(NULL, 0, &olen, fb->buf, fb->len);

    uint8_t *b64buf = allocBuffer(olen + 1);
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

    uint8_t *copy = allocBuffer(fb->len);
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
uint8_t qualityProfile() { return configuredProfile; }

}  // namespace Camera
