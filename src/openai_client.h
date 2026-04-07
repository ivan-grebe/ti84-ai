#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"
#include "wifi_manager.h"

namespace OpenAI {

// Conversation history (kept simple: system + last few exchanges)
static String conversationHistory = "";
static bool hasConversation = false;

void clearConversation() {
    conversationHistory = "";
    hasConversation = false;
    Serial.println("Conversation cleared");
}

// Escape a string for JSON
static String jsonEscape(const String &s) {
    String out;
    out.reserve(s.length() + 32);
    for (unsigned int i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

// Extract "content" value from JSON response (simple parser, no library needed)
static String extractContent(const String &json) {
    // Find "content":" in the response
    int idx = json.indexOf("\"content\":");
    if (idx < 0) return "Error: no content in response";

    idx = json.indexOf('"', idx + 10);
    if (idx < 0) return "Error: malformed response";
    idx++; // skip opening quote

    String content;
    while (idx < (int)json.length()) {
        char c = json[idx];
        if (c == '"') break;  // end of string
        if (c == '\\' && idx + 1 < (int)json.length()) {
            idx++;
            char next = json[idx];
            switch (next) {
                case 'n':  content += '\n'; break;
                case 't':  content += ' ';  break;
                case '"':  content += '"';  break;
                case '\\': content += '\\'; break;
                case 'r':  break;  // skip carriage returns
                default:   content += next; break;
            }
        } else {
            content += c;
        }
        idx++;
    }
    return content;
}

static bool ensureOpenAIHostResolves() {
    IPAddress resolved;
    if (WiFi.hostByName(OPENAI_HOST, resolved)) {
        Serial.printf("DNS OK: %s -> %s\n", OPENAI_HOST, resolved.toString().c_str());
        return true;
    }

    Serial.printf("DNS fail: dns1=%s dns2=%s\n",
                  WiFi.dnsIP(0).toString().c_str(),
                  WiFi.dnsIP(1).toString().c_str());
    Serial.println("Reconnecting WiFi to recover DNS...");
    WifiManager::disconnect();
    delay(250);
    if (!WifiManager::connect(10000)) {
        return false;
    }

    if (WiFi.hostByName(OPENAI_HOST, resolved)) {
        Serial.printf("DNS OK after reconnect: %s -> %s\n",
                      OPENAI_HOST, resolved.toString().c_str());
        return true;
    }

    Serial.printf("DNS still failing: dns1=%s dns2=%s\n",
                  WiFi.dnsIP(0).toString().c_str(),
                  WiFi.dnsIP(1).toString().c_str());
    return false;
}

// Make API call and return the response text
String chat(const String &userMessage,
            const String &imageBase64 = "",
            bool includeHistory = false,
            bool updateTextHistory = false) {
    if (!WifiManager::isConnected()) {
        return "Error: WiFi not connected";
    }

    String apiKey = WifiManager::getAPIKey();
    if (apiKey.length() == 0) {
        return "Error: No API key configured";
    }

    const bool isVision = imageBase64.length() > 0;
    const char *systemPrompt = isVision ? CAMERA_PROMPT : SYSTEM_PROMPT;
    const bool useHistory = !isVision && includeHistory &&
                            hasConversation && conversationHistory.length() > 0;
    String effectiveUserMessage = isVision ? "Read the image and answer." : userMessage;

    String model = OPENAI_MODEL;
    Serial.printf("Free heap before DNS: %u\n", ESP.getFreeHeap());
    if (!ensureOpenAIHostResolves()) {
        return "DNS ERROR";
    }

    String body;
    body.reserve(String(model).length() + effectiveUserMessage.length() + imageBase64.length() +
                 (useHistory ? conversationHistory.length() : 0) + strlen(systemPrompt) + 256);
    body += "{\"model\":\"";
    body += model;
    body += "\",\"messages\":[";
    body += "{\"role\":\"system\",\"content\":\"";
    body += jsonEscape(systemPrompt);
    body += "\"}";
    if (useHistory) {
        body += ",";
        body += conversationHistory;
    }
    body += ",{\"role\":\"user\",\"content\":";
    if (isVision) {
        body += "[{\"type\":\"text\",\"text\":\"";
        body += jsonEscape(effectiveUserMessage);
        body += "\"},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
        body += imageBase64;
        body += "\",\"detail\":\"high\"}}]";
    } else {
        body += "\"";
        body += jsonEscape(effectiveUserMessage);
        body += "\"";
    }
    body += "}],\"max_completion_tokens\":512,\"temperature\":0.3}";

    Serial.printf("API request: %u bytes, free heap after build: %u\n",
                  body.length(), ESP.getFreeHeap());
    String response;
    String rawResponse;

    auto runRequest = [&](String &out) {
        WiFiClientSecure client;
        client.setInsecure();  // Skip cert verification (ESP32 has limited CA store)
        client.setHandshakeTimeout(30);
        client.setTimeout(30);

        HTTPClient http;
        String url = "https://" + String(OPENAI_HOST) + String(OPENAI_PATH);
        if (!http.begin(client, url)) {
            out = "begin failed";
            return HTTPC_ERROR_CONNECTION_REFUSED;
        }

        http.setReuse(false);
        http.setConnectTimeout(15000);
        http.setTimeout(60000);  // 60s timeout for vision requests
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + apiKey);

        int code = http.POST(body);
        if (code > 0) {
            out = http.getString();
        } else {
            out = http.errorToString(code);
        }
        http.end();

        Serial.printf("API result code: %d\n", code);
        return code;
    };

    int httpCode = runRequest(rawResponse);
    if (httpCode <= 0) {
        Serial.printf("Transport error %d. Retrying once...\n", httpCode);
        delay(300);
        WifiManager::disconnect();
        delay(250);
        WifiManager::connect(10000);
        ensureOpenAIHostResolves();
        rawResponse = "";
        httpCode = runRequest(rawResponse);
    }

    if (httpCode == 200) {
        response = extractContent(rawResponse);

        // Save to conversation history (keep last exchange only to save memory)
        if (!isVision && updateTextHistory) {
            conversationHistory = "{\"role\":\"user\",\"content\":\"" +
                                  jsonEscape(userMessage) + "\"}," +
                                  "{\"role\":\"assistant\",\"content\":\"" +
                                  jsonEscape(response) + "\"}";
            hasConversation = true;
        }

        Serial.printf("Response: %u chars\n", response.length());
    } else if (httpCode > 0) {
        response = "API error " + String(httpCode) + ": " + rawResponse.substring(0, 200);
        Serial.println(response);
    } else {
        if (rawResponse == "connection refused") {
            response = "DNS/NET ERROR";
        } else {
        response = "HTTP error " + String(httpCode) + ": " + rawResponse;
        }
        Serial.println(response);
    }
    return response;
}

// Text query
String ask(const String &prompt) {
    clearConversation();
    return chat(prompt, "", false, true);
}

// Follow-up in conversation
String reply(const String &prompt) {
    return chat(prompt, "", true, true);
}

// Vision query with camera image
String solveImage(const String &imageBase64) {
    return chat("", imageBase64, false, false);
}

// Vision query with custom prompt
String askWithImage(const String &prompt, const String &imageBase64) {
    return chat(prompt, imageBase64, false, false);
}

}  // namespace OpenAI
