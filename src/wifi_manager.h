#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "esp_eap_client.h"
#include "esp_wifi.h"
#include "config.h"
#include "camera.h"
#include "web_ui.h"

namespace WifiManager {

static Preferences prefs;
static String savedSSID;
static String savedPass;
static String savedAPIKey;
static String savedEapIdentity;
static bool savedDebugApEnabled = DEFAULT_DEBUG_AP_ENABLED;
static uint8_t savedCameraProfile = CAM_PROFILE_DEFAULT;
static bool savedPhotoRecapEnabled = DEFAULT_PHOTO_RECAP_ENABLED;
static WebServer configServer(80);
static WebServer debugServer(81);
static bool portalActive = false;
static bool debugServerStarted = false;
static bool debugApStarted = false;
static bool connectPending = false;
static unsigned long connectDeadline = 0;

constexpr unsigned long kWifiTimeoutMs = 15000;
constexpr unsigned long kEnterpriseTimeoutMs = 30000;
constexpr unsigned long kEnterpriseAsyncTimeoutMs = 45000;

enum class ConnectResult : int8_t { Pending = 0, Connected = 1, Failed = -1 };

void startDebugServer();
void startDebugAccessPoint();

void cacheSettings(const String &ssid,
                   const String &pass,
                   const String &apiKey,
                   const String &eapIdentity,
                   bool debugApEnabled,
                   uint8_t cameraProfile,
                   bool photoRecapEnabled) {
    savedSSID = ssid;
    savedPass = pass;
    savedAPIKey = apiKey;
    savedEapIdentity = eapIdentity;
    savedDebugApEnabled = debugApEnabled;
    savedCameraProfile = normalizeCameraProfileValue(cameraProfile);
    savedPhotoRecapEnabled = photoRecapEnabled;
}

void sendJpegResponse(WebServer &server, const uint8_t *jpegData, size_t jpegLen) {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server.setContentLength(jpegLen);
    server.send(200, "image/jpeg", "");
    server.client().write(jpegData, jpegLen);
}

// NVS operations

void loadCredentials() {
    prefs.begin(NVS_NAMESPACE, true);
    cacheSettings(prefs.getString(NVS_KEY_SSID, ""),
                  prefs.getString(NVS_KEY_PASS, ""),
                  prefs.getString(NVS_KEY_APIKEY, ""),
                  prefs.getString(NVS_KEY_EAP_IDENTITY, ""),
                  prefs.getBool(NVS_KEY_DEBUGAP, DEFAULT_DEBUG_AP_ENABLED),
                  prefs.getUChar(NVS_KEY_CAMPROF, CAM_PROFILE_DEFAULT),
                  prefs.getBool(NVS_KEY_PHOTORECAP, DEFAULT_PHOTO_RECAP_ENABLED));
    prefs.end();
    Serial.printf("NVS loaded: SSID='%s' EAP='%s' pass_len=%d apikey_len=%d enterprise=%d\n",
                  savedSSID.c_str(), savedEapIdentity.c_str(),
                  savedPass.length(), savedAPIKey.length(), savedEapIdentity.length() > 0);
}

void saveSettings(const String &ssid,
                  const String &passInput,
                  const String &apiKeyInput,
                  const String &eapIdentity,
                  bool debugApEnabled,
                  uint8_t cameraProfile,
                  bool photoRecapEnabled) {
    const String effectivePass =
        (passInput.length() == 0 && ssid == savedSSID) ? savedPass : passInput;
    const String effectiveApiKey =
        (apiKeyInput.length() > 0) ? apiKeyInput : savedAPIKey;
    const uint8_t effectiveCameraProfile = normalizeCameraProfileValue(cameraProfile);

    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(NVS_KEY_SSID, ssid);
    prefs.putString(NVS_KEY_PASS, effectivePass);
    prefs.putString(NVS_KEY_APIKEY, effectiveApiKey);
    prefs.putString(NVS_KEY_EAP_IDENTITY, eapIdentity);
    prefs.putBool(NVS_KEY_DEBUGAP, debugApEnabled);
    prefs.putUChar(NVS_KEY_CAMPROF, effectiveCameraProfile);
    prefs.putBool(NVS_KEY_PHOTORECAP, photoRecapEnabled);
    prefs.end();

    cacheSettings(ssid,
                  effectivePass,
                  effectiveApiKey,
                  eapIdentity,
                  debugApEnabled,
                  effectiveCameraProfile,
                  photoRecapEnabled);
}

String getAPIKey() { return savedAPIKey; }
String getEapIdentity() { return savedEapIdentity; }
bool isEnterprise() { return savedEapIdentity.length() > 0; }
bool hasCredentials() { return savedSSID.length() > 0 && savedAPIKey.length() > 0; }
bool isDebugApEnabled() { return savedDebugApEnabled; }
uint8_t cameraQualityProfile() { return savedCameraProfile; }
bool photoRecapEnabled() { return savedPhotoRecapEnabled; }

void stopConfigPortal() {
    if (!portalActive) return;
    configServer.stop();
    portalActive = false;
    Serial.println("Config portal stopped");
}

// WiFi station mode

void startWiFiStation() {
    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);

    if (isEnterprise()) {
        Serial.printf("WPA2-Enterprise (PEAP) identity: %s\n", savedEapIdentity.c_str());
        esp_eap_client_set_identity((uint8_t *)savedEapIdentity.c_str(), savedEapIdentity.length());
        esp_eap_client_set_username((uint8_t *)savedEapIdentity.c_str(), savedEapIdentity.length());
        esp_eap_client_set_password((uint8_t *)savedPass.c_str(), savedPass.length());
        esp_wifi_sta_enterprise_enable();
        WiFi.begin(savedSSID.c_str());
    } else {
        WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    }
}

void onStationConnected() {
    startDebugServer();
    if (savedDebugApEnabled) {
        startDebugAccessPoint();
    }
    Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
}

bool connect(unsigned long timeoutMs = kWifiTimeoutMs) {
    if (savedSSID.length() == 0) return false;
    Serial.printf("Connecting to WiFi: %s\n", savedSSID.c_str());
    stopConfigPortal();
    debugApStarted = false;

    if (isEnterprise() && timeoutMs < kEnterpriseTimeoutMs) {
        timeoutMs = kEnterpriseTimeoutMs;
    }

    startWiFiStation();

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(500);
        Serial.printf("  WiFi status: %d (%lums)\n", WiFi.status(), millis() - start);
    }
    if (WiFi.status() == WL_CONNECTED) {
        onStationConnected();
        Serial.printf("DNS1=%s DNS2=%s\n",
                      WiFi.dnsIP(0).toString().c_str(),
                      WiFi.dnsIP(1).toString().c_str());
        Serial.printf("Camera debug page: http://%s:81/\n", WiFi.localIP().toString().c_str());
        if (savedDebugApEnabled) {
            Serial.printf("Camera debug AP: %s  http://%s:81/\n",
                          DEBUG_AP_SSID, DEBUG_AP_IP.toString().c_str());
        }
        return true;
    }
    Serial.println("WiFi connection failed");
    return false;
}

// Non-blocking connect: call beginConnect() once, then pollConnect() in loop().
bool beginConnect() {
    if (savedSSID.length() == 0) return false;
    Serial.printf("beginConnect: SSID='%s' enterprise=%d\n", savedSSID.c_str(), isEnterprise());
    stopConfigPortal();
    debugApStarted = false;

    startWiFiStation();

    connectPending = true;
    connectDeadline = millis() + (isEnterprise() ? kEnterpriseAsyncTimeoutMs : kWifiTimeoutMs);
    return true;
}

ConnectResult pollConnect() {
    if (!connectPending) return ConnectResult::Failed;
    if (WiFi.status() == WL_CONNECTED) {
        connectPending = false;
        onStationConnected();
        return ConnectResult::Connected;
    }
    if (millis() > connectDeadline) {
        connectPending = false;
        Serial.println("WiFi connection timed out");
        return ConnectResult::Failed;
    }
    return ConnectResult::Pending;
}

bool isConnectPending() { return connectPending; }

void disconnect() {
    connectPending = false;
    stopConfigPortal();
    debugApStarted = false;
    esp_wifi_sta_enterprise_disable();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi disconnected");
}

bool isConnected() { return WiFi.status() == WL_CONNECTED; }

void startDebugServer() {
    if (debugServerStarted) return;

    debugServer.on("/", HTTP_GET, []() {
        debugServer.send(200, "text/html", WebUi::DEBUG_HTML);
    });

    debugServer.on("/capture.jpg", HTTP_GET, []() {
        uint8_t *jpeg = nullptr;
        size_t jpegLen = 0;
        if (!Camera::capturePreviewJpeg(&jpeg, &jpegLen)) {
            debugServer.send(500, "text/plain", "Camera capture failed");
            return;
        }

        sendJpegResponse(debugServer, jpeg, jpegLen);
        Serial.printf("Served live preview: %u bytes\n", (unsigned)jpegLen);
        free(jpeg);
    });

    debugServer.on("/last.jpg", HTTP_GET, []() {
        if (!Camera::hasLastCapture()) {
            debugServer.send(404, "text/plain", "No last capture yet");
            return;
        }

        sendJpegResponse(debugServer, Camera::lastCaptureData(), Camera::lastCaptureSize());
        Serial.printf("Served last capture: %u bytes\n", (unsigned)Camera::lastCaptureSize());
    });

    debugServer.on("/status", HTTP_GET, []() {
        StaticJsonDocument<256> doc;
        doc["wifi"] = WiFi.localIP().toString();
        doc["debug_ap"] = savedDebugApEnabled;
        doc["camera_profile"] = savedCameraProfile;
        doc["has_last"] = Camera::hasLastCapture();
        doc["last_size"] = static_cast<unsigned>(Camera::lastCaptureSize());
        doc["last_width"] = Camera::lastCaptureWidth();
        doc["last_height"] = Camera::lastCaptureHeight();
        doc["last_ms"] = Camera::lastCaptureTime();

        String json;
        serializeJson(doc, json);
        debugServer.send(200, "application/json", json);
    });

    debugServer.begin();
    debugServerStarted = true;
    Serial.println("Camera debug server running on port 81");
}

void startDebugAccessPoint() {
    if (!savedDebugApEnabled) {
        return;
    }
    if (debugApStarted) return;

    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAPConfig(DEBUG_AP_IP, DEBUG_AP_IP, IPAddress(255, 255, 255, 0))) {
        Serial.println("Debug AP config failed");
        return;
    }
    if (!WiFi.softAP(DEBUG_AP_SSID, DEBUG_AP_PASS)) {
        Serial.println("Debug AP start failed");
        return;
    }

    debugApStarted = true;
    Serial.printf("Debug AP started: %s (pass: %s)\n", DEBUG_AP_SSID, DEBUG_AP_PASS);
}

// Non-blocking config portal.
// Call startConfigPortal() once, then handleConfigPortal() in loop().

void startConfigPortal() {
    if (portalActive) return;

    Serial.println("Starting config portal...");
    debugApStarted = false;
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, AP_PASS);
    startDebugServer();
    Serial.printf("AP started: %s (pass: %s)\n", AP_SSID, AP_PASS);
    Serial.printf("Config page: http://%s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("Camera debug page: http://%s:81/\n", WiFi.softAPIP().toString().c_str());

    configServer.on("/", HTTP_GET, []() {
        WebUi::PortalState state;
        state.savedSSID = savedSSID;
        state.savedEapIdentity = savedEapIdentity;
        state.hasApiKey = savedAPIKey.length() > 0;
        state.debugApEnabled = savedDebugApEnabled;
        state.cameraProfile = savedCameraProfile;
        state.photoRecapEnabled = savedPhotoRecapEnabled;
        configServer.send(200, "text/html", WebUi::renderConfigHtml(state));
    });

    configServer.on("/save", HTTP_POST, []() {
        const String ssid = configServer.arg("ssid");
        const String pass = configServer.arg("pass");
        const String apikey = configServer.arg("apikey");
        const String eapIdentity = configServer.arg("eap_identity");
        const bool debugApEnabled = configServer.hasArg("debugap");
        const bool photoRecapEnabled = configServer.hasArg("photo_recap");
        const uint8_t cameraProfile =
            normalizeCameraProfileValue(
                static_cast<uint8_t>(configServer.arg("cam_profile").toInt()));
        const String effectiveApiKey = apikey.length() > 0 ? apikey : savedAPIKey;

        if (ssid.length() == 0 || effectiveApiKey.length() == 0) {
            configServer.send(400, "text/plain", "WiFi SSID and API key are required");
            return;
        }

        saveSettings(ssid,
                     pass,
                     apikey,
                     eapIdentity,
                     debugApEnabled,
                     cameraProfile,
                     photoRecapEnabled);
        configServer.send(200, "text/html", WebUi::SAVED_HTML);
        delay(2000);
        ESP.restart();
    });

    configServer.begin();
    portalActive = true;
    Serial.println("Config portal running.");
}

void handleConfigPortal() {
    if (portalActive) {
        configServer.handleClient();
    }
    if (debugServerStarted) {
        debugServer.handleClient();
    }
}

bool isPortalActive() { return portalActive; }

}  // namespace WifiManager
