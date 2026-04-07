#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "config.h"
#include "camera.h"

namespace WifiManager {

static Preferences prefs;
static String savedSSID;
static String savedPass;
static String savedAPIKey;
static WebServer configServer(80);
static WebServer debugServer(81);
static bool portalActive = false;
static bool debugServerStarted = false;
static bool debugApStarted = false;

void startDebugServer();
void startDebugAccessPoint();

// ── NVS operations ──

void loadCredentials() {
    prefs.begin(NVS_NAMESPACE, true);
    savedSSID   = prefs.getString(NVS_KEY_SSID, "");
    savedPass   = prefs.getString(NVS_KEY_PASS, "");
    savedAPIKey = prefs.getString(NVS_KEY_APIKEY, "");
    prefs.end();
}

void saveCredentials(const String &ssid, const String &pass, const String &apiKey) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(NVS_KEY_SSID, ssid);
    prefs.putString(NVS_KEY_PASS, pass);
    prefs.putString(NVS_KEY_APIKEY, apiKey);
    prefs.end();
    savedSSID   = ssid;
    savedPass   = pass;
    savedAPIKey = apiKey;
}

String getAPIKey() { return savedAPIKey; }
bool hasCredentials() { return savedSSID.length() > 0 && savedAPIKey.length() > 0; }

void stopConfigPortal() {
    if (!portalActive) return;
    configServer.stop();
    portalActive = false;
    Serial.println("Config portal stopped");
}

// ── WiFi STA ──

bool connect(unsigned long timeoutMs = 15000) {
    if (savedSSID.length() == 0) return false;
    Serial.printf("Connecting to WiFi: %s\n", savedSSID.c_str());
    stopConfigPortal();
    debugApStarted = false;
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
        startDebugAccessPoint();
        startDebugServer();
        Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("DNS1=%s DNS2=%s\n",
                      WiFi.dnsIP(0).toString().c_str(),
                      WiFi.dnsIP(1).toString().c_str());
        Serial.printf("Camera debug page: http://%s:81/\n", WiFi.localIP().toString().c_str());
        Serial.printf("Camera debug AP: %s  http://%s:81/\n",
                      DEBUG_AP_SSID, DEBUG_AP_IP.toString().c_str());
        return true;
    }
    Serial.println("WiFi connection failed");
    return false;
}

void disconnect() {
    stopConfigPortal();
    debugApStarted = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi disconnected");
}

bool isConnected() { return WiFi.status() == WL_CONNECTED; }

// ── Config Portal HTML ──

static const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TI-84 AI Setup</title>
<style>
body{font-family:monospace;max-width:400px;margin:40px auto;padding:0 20px;background:#1a1a2e;color:#e0e0e0}
h1{color:#00d4aa;text-align:center}
label{display:block;margin:12px 0 4px;color:#00d4aa}
input{width:100%;padding:8px;box-sizing:border-box;background:#16213e;border:1px solid #00d4aa;color:#e0e0e0;border-radius:4px}
button{width:100%;padding:12px;margin-top:20px;background:#00d4aa;color:#1a1a2e;border:none;border-radius:4px;font-weight:bold;font-size:16px;cursor:pointer}
button:hover{background:#00b894}
.info{font-size:12px;color:#888;margin-top:4px}
</style>
</head>
<body>
<h1>TI-84 AI Setup</h1>
<form method="POST" action="/save">
<label>WiFi Network Name</label>
<input name="ssid" required placeholder="Your WiFi SSID">
<label>WiFi Password</label>
<input name="pass" type="password" placeholder="Leave blank if open">
<label>OpenAI API Key</label>
<input name="apikey" required placeholder="sk-...">
<div class="info">Get yours at platform.openai.com/api-keys</div>
<button type="submit">Save and Restart</button>
</form>
</body>
</html>
)rawliteral";

static const char SAVED_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Saved</title>
<style>
body{font-family:monospace;max-width:400px;margin:40px auto;padding:0 20px;background:#1a1a2e;color:#e0e0e0;text-align:center}
h1{color:#00d4aa}
</style>
</head>
<body>
<h1>Saved!</h1>
<p>Credentials saved. The device will now restart and connect to your WiFi.</p>
<p>Run prgmTIAI on your calculator.</p>
</body>
</html>
)rawliteral";

static const char DEBUG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TI-84 AI Camera Debug</title>
<style>
body{font-family:monospace;max-width:900px;margin:20px auto;padding:0 16px;background:#10151c;color:#e8f0f7}
h1{color:#86efac}
a,button{color:#10151c;background:#86efac;border:none;border-radius:6px;padding:10px 14px;text-decoration:none;font-weight:bold;cursor:pointer}
.row{display:flex;gap:12px;flex-wrap:wrap;margin:14px 0}
.card{background:#182230;border:1px solid #2a3b50;border-radius:10px;padding:12px;flex:1;min-width:280px}
img{width:100%;height:auto;border-radius:8px;background:#0b0f14;border:1px solid #2a3b50}
code{color:#93c5fd}
</style>
</head>
<body>
<h1>TI-84 AI Camera Debug</h1>
<p>Use this to tune the actual JPEG coming from the ESP32 camera.</p>
<div class="row">
<button onclick="refreshLive()">Capture Fresh Preview</button>
<button onclick="refreshLast()">Refresh Last Sent Image</button>
</div>
<div class="card">
<p><strong>Live preview</strong> from the camera right now: <code>/capture.jpg</code></p>
<img id="live" src="/capture.jpg?ts=0" alt="Live camera preview">
</div>
<div class="card">
<p><strong>Last image sent to OpenAI</strong>: <code>/last.jpg</code></p>
<img id="last" src="/last.jpg?ts=0" alt="Last image sent to OpenAI">
</div>
<script>
function refreshLive(){document.getElementById('live').src='/capture.jpg?ts='+Date.now();}
function refreshLast(){document.getElementById('last').src='/last.jpg?ts='+Date.now();}
refreshLast();
</script>
</body>
</html>
)rawliteral";

void startDebugServer() {
    if (debugServerStarted) return;

    debugServer.on("/", HTTP_GET, []() {
        debugServer.send(200, "text/html", DEBUG_HTML);
    });

    debugServer.on("/capture.jpg", HTTP_GET, []() {
        uint8_t *jpeg = nullptr;
        size_t jpegLen = 0;
        if (!Camera::capturePreviewJpeg(&jpeg, &jpegLen)) {
            debugServer.send(500, "text/plain", "Camera capture failed");
            return;
        }

        debugServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
        debugServer.setContentLength(jpegLen);
        debugServer.send(200, "image/jpeg", "");
        debugServer.client().write(jpeg, jpegLen);
        Serial.printf("Served live preview: %u bytes\n", (unsigned)jpegLen);
        free(jpeg);
    });

    debugServer.on("/last.jpg", HTTP_GET, []() {
        if (!Camera::hasLastCapture()) {
            debugServer.send(404, "text/plain", "No last capture yet");
            return;
        }

        debugServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
        debugServer.setContentLength(Camera::lastCaptureSize());
        debugServer.send(200, "image/jpeg", "");
        debugServer.client().write(Camera::lastCaptureData(), Camera::lastCaptureSize());
        Serial.printf("Served last capture: %u bytes\n", (unsigned)Camera::lastCaptureSize());
    });

    debugServer.on("/status", HTTP_GET, []() {
        String json = "{";
        json += "\"wifi\":\"" + WiFi.localIP().toString() + "\",";
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

// ── Non-blocking config portal ──
// Call startConfigPortal() once, then handleConfigPortal() in loop().
// This allows CBL2 to keep running alongside the web server.

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
        configServer.send(200, "text/html", CONFIG_HTML);
    });

    configServer.on("/save", HTTP_POST, []() {
        String ssid   = configServer.arg("ssid");
        String pass   = configServer.arg("pass");
        String apikey = configServer.arg("apikey");

        if (ssid.length() > 0 && apikey.length() > 0) {
            saveCredentials(ssid, pass, apikey);
            configServer.send(200, "text/html", SAVED_HTML);
            delay(2000);
            ESP.restart();
        } else {
            configServer.send(400, "text/plain", "Missing required fields");
        }
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
