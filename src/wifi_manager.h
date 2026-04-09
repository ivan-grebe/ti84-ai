#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "esp_wpa2.h"
#include "config.h"
#include "camera.h"
#include "web_ui.h"

namespace WifiManager {

static Preferences prefs;
static String savedSSID;
static String savedPass;
static uint8_t savedWifiAuthMode = WIFI_AUTH_MODE_DEFAULT;
static String savedEnterpriseIdentity;
static String savedEnterpriseUsername;
static String savedEnterprisePassword;
static String savedAPIKey;
static bool savedDebugApEnabled = DEFAULT_DEBUG_AP_ENABLED;
static uint8_t savedCameraProfile = CAM_PROFILE_DEFAULT;
static WebServer configServer(80);
static WebServer debugServer(81);
static bool portalActive = false;
static bool debugServerStarted = false;
static bool debugApStarted = false;

void startDebugServer();
void startDebugAccessPoint();

void cacheSettings(const String &ssid,
                   const String &pass,
                   uint8_t wifiAuthMode,
                   const String &enterpriseIdentity,
                   const String &enterpriseUsername,
                   const String &enterprisePassword,
                   const String &apiKey,
                   bool debugApEnabled,
                   uint8_t cameraProfile) {
    savedSSID = ssid;
    savedPass = pass;
    savedWifiAuthMode = normalizeWifiAuthModeValue(wifiAuthMode);
    savedEnterpriseIdentity = enterpriseIdentity;
    savedEnterpriseUsername = enterpriseUsername;
    savedEnterprisePassword = enterprisePassword;
    savedAPIKey = apiKey;
    savedDebugApEnabled = debugApEnabled;
    savedCameraProfile = normalizeCameraProfileValue(cameraProfile);
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
                  prefs.getUChar(NVS_KEY_WIFIMODE, WIFI_AUTH_MODE_DEFAULT),
                  prefs.getString(NVS_KEY_EAPIDENT, ""),
                  prefs.getString(NVS_KEY_EAPUSER, ""),
                  prefs.getString(NVS_KEY_EAPPASS, ""),
                  prefs.getString(NVS_KEY_APIKEY, ""),
                  prefs.getBool(NVS_KEY_DEBUGAP, DEFAULT_DEBUG_AP_ENABLED),
                  prefs.getUChar(NVS_KEY_CAMPROF, CAM_PROFILE_DEFAULT));
    prefs.end();
}

void saveSettings(const String &ssid,
                  const String &passInput,
                  uint8_t wifiAuthMode,
                  const String &enterpriseIdentityInput,
                  const String &enterpriseUsernameInput,
                  const String &enterprisePasswordInput,
                  const String &apiKeyInput,
                  bool debugApEnabled,
                  uint8_t cameraProfile) {
    const uint8_t effectiveWifiAuthMode = normalizeWifiAuthModeValue(wifiAuthMode);
    const String effectivePass =
        (passInput.length() == 0 && ssid == savedSSID) ? savedPass : passInput;
    const String effectiveEnterprisePassword =
        (enterprisePasswordInput.length() == 0 && ssid == savedSSID &&
         effectiveWifiAuthMode == savedWifiAuthMode)
            ? savedEnterprisePassword
            : enterprisePasswordInput;
    const String effectiveApiKey =
        (apiKeyInput.length() > 0) ? apiKeyInput : savedAPIKey;
    const uint8_t effectiveCameraProfile = normalizeCameraProfileValue(cameraProfile);

    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(NVS_KEY_SSID, ssid);
    prefs.putString(NVS_KEY_PASS, effectivePass);
    prefs.putUChar(NVS_KEY_WIFIMODE, effectiveWifiAuthMode);
    prefs.putString(NVS_KEY_EAPIDENT, enterpriseIdentityInput);
    prefs.putString(NVS_KEY_EAPUSER, enterpriseUsernameInput);
    prefs.putString(NVS_KEY_EAPPASS, effectiveEnterprisePassword);
    prefs.putString(NVS_KEY_APIKEY, effectiveApiKey);
    prefs.putBool(NVS_KEY_DEBUGAP, debugApEnabled);
    prefs.putUChar(NVS_KEY_CAMPROF, effectiveCameraProfile);
    prefs.end();

    cacheSettings(ssid,
                  effectivePass,
                  effectiveWifiAuthMode,
                  enterpriseIdentityInput,
                  enterpriseUsernameInput,
                  effectiveEnterprisePassword,
                  effectiveApiKey,
                  debugApEnabled,
                  effectiveCameraProfile);
}

String getAPIKey() { return savedAPIKey; }
bool hasCredentials() {
    if (savedSSID.length() == 0 || savedAPIKey.length() == 0) {
        return false;
    }
    if (normalizeWifiAuthModeValue(savedWifiAuthMode) == WIFI_AUTH_MODE_ENTERPRISE_PEAP) {
        return savedEnterpriseUsername.length() > 0 && savedEnterprisePassword.length() > 0;
    }
    return true;
}
bool isDebugApEnabled() { return savedDebugApEnabled; }
uint8_t cameraQualityProfile() { return savedCameraProfile; }
bool isEnterpriseMode() {
    return normalizeWifiAuthModeValue(savedWifiAuthMode) == WIFI_AUTH_MODE_ENTERPRISE_PEAP;
}

void clearEnterpriseConfig() {
    esp_wifi_sta_wpa2_ent_disable();
    esp_wifi_sta_wpa2_ent_clear_identity();
    esp_wifi_sta_wpa2_ent_clear_username();
    esp_wifi_sta_wpa2_ent_clear_password();
    esp_wifi_sta_wpa2_ent_clear_new_password();
    esp_wifi_sta_wpa2_ent_clear_ca_cert();
    esp_wifi_sta_wpa2_ent_clear_cert_key();
}

void stopConfigPortal() {
    if (!portalActive) return;
    configServer.stop();
    portalActive = false;
    Serial.println("Config portal stopped");
}

// WiFi station mode

bool connect(unsigned long timeoutMs = 15000) {
    if (savedSSID.length() == 0) return false;
    Serial.printf("Connecting to WiFi: %s\n", savedSSID.c_str());
    stopConfigPortal();
    debugApStarted = false;
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    clearEnterpriseConfig();

    if (isEnterpriseMode()) {
        if (savedEnterpriseUsername.length() == 0 || savedEnterprisePassword.length() == 0) {
            Serial.println("Enterprise WiFi credentials missing");
            return false;
        }

        const String identity = savedEnterpriseIdentity.length() > 0
                                    ? savedEnterpriseIdentity
                                    : savedEnterpriseUsername;
        Serial.printf("Using WPA2 Enterprise (PEAP) identity=%s username=%s\n",
                      identity.c_str(),
                      savedEnterpriseUsername.c_str());
        WiFi.begin(savedSSID.c_str(),
                   WPA2_AUTH_PEAP,
                   identity.c_str(),
                   savedEnterpriseUsername.c_str(),
                   savedEnterprisePassword.c_str());
    } else {
        if (savedPass.length() > 0) {
            WiFi.begin(savedSSID.c_str(), savedPass.c_str());
        } else {
            Serial.println("Connecting to open network");
            WiFi.begin(savedSSID.c_str());
        }
    }

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
        startDebugServer();
        if (savedDebugApEnabled) {
            startDebugAccessPoint();
        } else {
            Serial.println("TI84CAM hotspot disabled in settings");
        }
        Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
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

void disconnect() {
    stopConfigPortal();
    debugApStarted = false;
    WiFi.disconnect(true);
    clearEnterpriseConfig();
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
        String json = "{";
        json += "\"wifi\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"debug_ap\":" + String(savedDebugApEnabled ? "true" : "false") + ",";
        json += "\"camera_profile\":" + String(savedCameraProfile) + ",";
        json += "\"has_last\":" + String(Camera::hasLastCapture() ? "true" : "false") + ",";
        json += "\"last_size\":" + String((unsigned)Camera::lastCaptureSize()) + ",";
        json += "\"last_width\":" + String(Camera::lastCaptureWidth()) + ",";
        json += "\"last_height\":" + String(Camera::lastCaptureHeight()) + ",";
        json += "\"last_ms\":" + String(Camera::lastCaptureTime());
        json += "}";
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
        state.hasApiKey = savedAPIKey.length() > 0;
        state.debugApEnabled = savedDebugApEnabled;
        state.cameraProfile = savedCameraProfile;
        state.wifiAuthMode = savedWifiAuthMode;
        state.enterpriseIdentity = savedEnterpriseIdentity;
        state.enterpriseUsername = savedEnterpriseUsername;
        state.hasEnterprisePassword = savedEnterprisePassword.length() > 0;
        configServer.send(200, "text/html", WebUi::renderConfigHtml(state));
    });

    configServer.on("/save", HTTP_POST, []() {
        const String ssid = configServer.arg("ssid");
        const String pass = configServer.arg("pass");
        const uint8_t wifiAuthMode =
            normalizeWifiAuthModeValue(
                static_cast<uint8_t>(configServer.arg("wifi_mode").toInt()));
        const String enterpriseIdentity = configServer.arg("eap_identity");
        const String enterpriseUsername = configServer.arg("eap_username");
        const String enterprisePassword = configServer.arg("eap_password");
        const String apikey = configServer.arg("apikey");
        const bool debugApEnabled = configServer.hasArg("debugap");
        const uint8_t cameraProfile =
            normalizeCameraProfileValue(
                static_cast<uint8_t>(configServer.arg("cam_profile").toInt()));
        const String effectiveApiKey = apikey.length() > 0 ? apikey : savedAPIKey;
        const String effectiveEnterprisePassword =
            (enterprisePassword.length() > 0 ||
             !(wifiAuthMode == savedWifiAuthMode && ssid == savedSSID))
                ? enterprisePassword
                : savedEnterprisePassword;

        if (ssid.length() == 0 || effectiveApiKey.length() == 0) {
            configServer.send(400, "text/plain", "WiFi SSID and API key are required");
            return;
        }
        if (wifiAuthMode == WIFI_AUTH_MODE_ENTERPRISE_PEAP &&
            (enterpriseUsername.length() == 0 || effectiveEnterprisePassword.length() == 0)) {
            configServer.send(400,
                              "text/plain",
                              "Enterprise username and password are required");
            return;
        }

        saveSettings(ssid,
                     pass,
                     wifiAuthMode,
                     enterpriseIdentity,
                     enterpriseUsername,
                     enterprisePassword,
                     apikey,
                     debugApEnabled,
                     cameraProfile);
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
