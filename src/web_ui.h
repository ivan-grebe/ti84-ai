#pragma once

#include <Arduino.h>

#include "config.h"

namespace WebUi {

struct PortalState {
    String savedSSID;
    bool hasApiKey = false;
    bool debugApEnabled = DEFAULT_DEBUG_AP_ENABLED;
    uint8_t cameraProfile = CAM_PROFILE_DEFAULT;
    uint8_t wifiAuthMode = WIFI_AUTH_MODE_DEFAULT;
    String enterpriseIdentity;
    String enterpriseUsername;
    bool hasEnterprisePassword = false;
    bool photoRecapEnabled = DEFAULT_PHOTO_RECAP_ENABLED;
};

const char *cameraProfileLabel(uint8_t profile) {
    switch (normalizeCameraProfileValue(profile)) {
        case CAM_PROFILE_LOW:
            return "Fast (VGA)";
        case CAM_PROFILE_HIGH:
            return "Sharp (UXGA)";
        case CAM_PROFILE_BALANCED:
        default:
            return "Balanced (SXGA)";
    }
}

const char *wifiAuthModeLabel(uint8_t mode) {
    switch (normalizeWifiAuthModeValue(mode)) {
        case WIFI_AUTH_MODE_ENTERPRISE_PEAP:
            return "WPA2 Enterprise (PEAP)";
        case WIFI_AUTH_MODE_PERSONAL:
        default:
            return "Personal / Open";
    }
}

String htmlEscape(const String &value) {
    String out;
    out.reserve(value.length() + 16);
    for (unsigned int i = 0; i < value.length(); i++) {
        switch (value[i]) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&#39;";
                break;
            default:
                out += value[i];
                break;
        }
    }
    return out;
}

String checkedAttr(bool value) {
    return value ? " checked" : "";
}

String selectedCameraAttr(uint8_t current, uint8_t expected) {
    return normalizeCameraProfileValue(current) == normalizeCameraProfileValue(expected)
               ? " selected"
               : "";
}

String selectedWifiAuthAttr(uint8_t current, uint8_t expected) {
    return normalizeWifiAuthModeValue(current) == normalizeWifiAuthModeValue(expected)
               ? " selected"
               : "";
}

String renderConfigHtml(const PortalState &state) {
    String html;
    html.reserve(4200);
    html += R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TI-84 AI Setup</title>
<style>
body{font-family:monospace;max-width:460px;margin:32px auto;padding:0 20px;background:#1a1a2e;color:#e0e0e0}
h1{color:#00d4aa;text-align:center}
label{display:block;margin:14px 0 4px;color:#00d4aa}
input,select{width:100%;padding:10px;box-sizing:border-box;background:#16213e;border:1px solid #00d4aa;color:#e0e0e0;border-radius:4px}
button{width:100%;padding:12px;margin-top:20px;background:#00d4aa;color:#1a1a2e;border:none;border-radius:4px;font-weight:bold;font-size:16px;cursor:pointer}
button:hover{background:#00b894}
.info{font-size:12px;color:#98a6b3;margin-top:4px;line-height:1.4}
.status{background:#16213e;border:1px solid #2dd4bf;border-radius:6px;padding:10px;margin:16px 0}
.toggle{display:flex;align-items:center;gap:10px;margin-top:14px}
.toggle input{width:auto;transform:scale(1.2)}
</style>
</head>
<body>
<h1>TI-84 AI Setup</h1>
<div class="status">
<div>Saved WiFi: )rawliteral";
    html += state.savedSSID.length() > 0 ? htmlEscape(state.savedSSID) : "(none)";
    html += R"rawliteral(</div>
<div>Saved API key: )rawliteral";
    html += state.hasApiKey ? "present" : "missing";
    html += R"rawliteral(</div>
<div>WiFi security: )rawliteral";
    html += wifiAuthModeLabel(state.wifiAuthMode);
    html += R"rawliteral(</div>
<div>TI84CAM hotspot: )rawliteral";
    html += state.debugApEnabled ? "enabled" : "disabled";
    html += R"rawliteral(</div>
<div>Camera preset: )rawliteral";
    html += cameraProfileLabel(state.cameraProfile);
    html += R"rawliteral(</div>
<div>Photo recap: )rawliteral";
    html += state.photoRecapEnabled ? "enabled" : "disabled";
    html += R"rawliteral(</div>
</div>
<form method="POST" action="/save">
<label>WiFi Network Name</label>
<input name="ssid" required placeholder="Your WiFi SSID" value=")rawliteral";
    html += htmlEscape(state.savedSSID);
    html += R"rawliteral(">
<label>WiFi Security</label>
<select name="wifi_mode">
<option value="0")rawliteral";
    html += selectedWifiAuthAttr(state.wifiAuthMode, WIFI_AUTH_MODE_PERSONAL);
    html += R"rawliteral(>Personal / Open</option>
<option value="1")rawliteral";
    html += selectedWifiAuthAttr(state.wifiAuthMode, WIFI_AUTH_MODE_ENTERPRISE_PEAP);
    html += R"rawliteral(>WPA2 Enterprise (PEAP)</option>
</select>
<div class="info">Use WPA2 Enterprise for common eduroam-style username/password networks.</div>
<label>WiFi Password</label>
<input name="pass" type="password" placeholder="Leave blank to keep current password for this SSID">
<div class="info">Only used for Personal / Open networks. If you change to a new SSID and leave this blank, the network is treated as open.</div>
<label>Enterprise Identity</label>
<input name="eap_identity" placeholder="Optional outer identity. Leave blank to use username." value=")rawliteral";
    html += htmlEscape(state.enterpriseIdentity);
    html += R"rawliteral(">
<div class="info">For many eduroam setups, identity can be the same as username. Some schools use an anonymous outer identity here.</div>
<label>Enterprise Username</label>
<input name="eap_username" placeholder="Required for WPA2 Enterprise" value=")rawliteral";
    html += htmlEscape(state.enterpriseUsername);
    html += R"rawliteral(">
<label>Enterprise Password</label>
<input name="eap_password" type="password" placeholder="Leave blank to keep current enterprise password">
<div class="info">Current enterprise password is )rawliteral";
    html += state.hasEnterprisePassword ? "saved." : "not saved yet.";
    html += R"rawliteral( Only used for WPA2 Enterprise.</div>
<label>OpenAI API Key</label>
<input name="apikey" placeholder="Leave blank to keep current key">
<div class="info">Current key is )rawliteral";
    html += state.hasApiKey ? "saved." : "not saved yet.";
    html += R"rawliteral( Get yours at platform.openai.com/api-keys</div>
<div class="toggle">
<input id="debugap" name="debugap" type="checkbox")rawliteral";
    html += checkedAttr(state.debugApEnabled);
    html += R"rawliteral(>
<label for="debugap" style="margin:0">Enable TI84CAM hotspot</label>
</div>
<div class="info">When enabled, the camera debug page is also exposed over the TI84CAM WiFi hotspot after CONNECT.</div>
<div class="toggle">
<input id="photo_recap" name="photo_recap" type="checkbox")rawliteral";
    html += checkedAttr(state.photoRecapEnabled);
    html += R"rawliteral(>
<label for="photo_recap" style="margin:0">Repeat image text before answer</label>
</div>
<div class="info">When enabled, photo answers first restate the readable problem text in plain words, then solve it.</div>
<label>Camera Quality</label>
<select name="cam_profile">
<option value="0")rawliteral";
    html += selectedCameraAttr(state.cameraProfile, CAM_PROFILE_LOW);
    html += R"rawliteral(>Fast (VGA)</option>
<option value="1")rawliteral";
    html += selectedCameraAttr(state.cameraProfile, CAM_PROFILE_BALANCED);
    html += R"rawliteral(>Balanced (SXGA)</option>
<option value="2")rawliteral";
    html += selectedCameraAttr(state.cameraProfile, CAM_PROFILE_HIGH);
    html += R"rawliteral(>Sharp (UXGA)</option>
</select>
<div class="info">Higher quality helps image reading but makes uploads slower and less tolerant of weak WiFi.</div>
<button type="submit">Save and Restart</button>
</form>
</body>
</html>
)rawliteral";
    return html;
}

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
<p>Credentials saved. The device will now restart.</p>
<p>After reboot, run prgmTIAI and use CONNECT from the calculator.</p>
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

}  // namespace WebUi
