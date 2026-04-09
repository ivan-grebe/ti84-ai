#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "wifi_manager.h"

namespace OpenAI {

namespace {

constexpr uint16_t kMaxOutputTokens = 512;
constexpr float kRequestTemperature = 0.3f;
constexpr unsigned long kWifiReconnectDelayMs = 250;
constexpr unsigned long kTransportRetryDelayMs = 300;
constexpr unsigned long kWifiReconnectTimeoutMs = 10000;
constexpr uint16_t kHttpConnectTimeoutMs = 15000;
constexpr uint32_t kHttpRequestTimeoutMs = 60000;
constexpr size_t kResponseParseSlack = 4096;
constexpr size_t kErrorParseSlack = 2048;
constexpr size_t kResponseIdDocCapacity = 128;
constexpr char kDefaultVisionPrompt[] = "Read the image and answer.";

struct RequestOptions {
    String userMessage;
    String imageBase64;
    const char *instructions = "";
    bool isVision = false;
    bool includePreviousResponse = false;
    bool updateConversationState = false;
};

struct RequestResult {
    int httpCode = HTTPC_ERROR_CONNECTION_REFUSED;
    String rawBody = "";
};

String previousResponseId = "";

void appendJsonString(String &out, const String &value) {
    out += '"';
    for (unsigned int i = 0; i < value.length(); i++) {
        const char c = value[i];
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    out += '"';
}

bool shouldStoreResponse(const RequestOptions &options) {
    return !options.isVision && options.updateConversationState;
}

bool shouldUsePreviousResponse(const RequestOptions &options) {
    return options.includePreviousResponse && previousResponseId.length() > 0;
}

void logDnsState(const char *prefix) {
    Serial.printf("%s dns1=%s dns2=%s\n",
                  prefix,
                  WiFi.dnsIP(0).toString().c_str(),
                  WiFi.dnsIP(1).toString().c_str());
}

bool ensureOpenAIHostResolves() {
    IPAddress resolved;
    if (WiFi.hostByName(OPENAI_HOST, resolved)) {
        Serial.printf("DNS OK: %s -> %s\n", OPENAI_HOST, resolved.toString().c_str());
        return true;
    }

    logDnsState("DNS fail:");
    Serial.println("Reconnecting WiFi to recover DNS...");
    WifiManager::disconnect();
    delay(kWifiReconnectDelayMs);
    if (!WifiManager::connect(kWifiReconnectTimeoutMs)) {
        return false;
    }

    if (WiFi.hostByName(OPENAI_HOST, resolved)) {
        Serial.printf("DNS OK after reconnect: %s -> %s\n",
                      OPENAI_HOST, resolved.toString().c_str());
        return true;
    }

    logDnsState("DNS still failing:");
    return false;
}

void addTextInputMessage(JsonArray input, const char *role, const String &text) {
    JsonObject message = input.createNestedObject();
    message["role"] = role;
    JsonArray content = message.createNestedArray("content");
    JsonObject part = content.createNestedObject();
    part["type"] = "input_text";
    part["text"] = text;
}

String buildTextRequestBody(const RequestOptions &options) {
    const size_t capacity =
        1536 + strlen(options.instructions) + options.userMessage.length() + previousResponseId.length();
    DynamicJsonDocument doc(capacity);

    doc["model"] = OPENAI_MODEL;
    doc["instructions"] = options.instructions;
    doc["temperature"] = kRequestTemperature;
    doc["max_output_tokens"] = kMaxOutputTokens;
    doc["store"] = shouldStoreResponse(options);

    if (shouldUsePreviousResponse(options)) {
        doc["previous_response_id"] = previousResponseId;
    }

    JsonArray input = doc.createNestedArray("input");
    addTextInputMessage(input, "user", options.userMessage);

    String body;
    body.reserve(capacity);
    serializeJson(doc, body);
    return body;
}

String buildVisionRequestBody(const RequestOptions &options) {
    // This path is intentionally manual. Fully materializing the image request in
    // ArduinoJson would duplicate a very large base64 string in RAM, which makes
    // TLS uploads noticeably less reliable on the ESP32. The text-only path can
    // use ArduinoJson safely because those payloads stay small.
    const String prompt = options.userMessage.length() > 0
        ? options.userMessage
        : String(kDefaultVisionPrompt);

    String body;
    // Reserve once so we do not keep reallocating while appending a 100KB+ image.
    body.reserve(strlen(OPENAI_MODEL) + strlen(options.instructions) + prompt.length() +
                 options.imageBase64.length() + 256);
    body += "{\"model\":";
    appendJsonString(body, OPENAI_MODEL);
    body += ",\"instructions\":";
    appendJsonString(body, options.instructions);
    body += ",\"input\":[{\"role\":\"user\",\"content\":[{\"type\":\"input_text\",\"text\":";
    appendJsonString(body, prompt);
    body += "},{\"type\":\"input_image\",\"image_url\":\"data:image/jpeg;base64,";
    body += options.imageBase64;
    body += "\"}]}],\"max_output_tokens\":";
    body += String(kMaxOutputTokens);
    body += ",\"temperature\":";
    body += String(kRequestTemperature, 1);
    body += ",\"store\":false}";
    return body;
}

String buildRequestBody(const RequestOptions &options) {
    return options.isVision ? buildVisionRequestBody(options)
                            : buildTextRequestBody(options);
}

String extractOutputText(const String &rawResponse) {
    DynamicJsonDocument doc(rawResponse.length() + kResponseParseSlack);
    const DeserializationError err = deserializeJson(doc, rawResponse);
    if (err) {
        return "Error: malformed response";
    }

    JsonArray output = doc["output"].as<JsonArray>();
    if (output.isNull()) {
        return "Error: no content in response";
    }

    String content;
    for (JsonObject item : output) {
        const char *itemType = item["type"] | "";
        if (strcmp(itemType, "message") != 0) {
            continue;
        }

        JsonArray parts = item["content"].as<JsonArray>();
        for (JsonObject part : parts) {
            const char *partType = part["type"] | "";
            const char *text = part["text"] | "";
            if ((strcmp(partType, "output_text") == 0 || strcmp(partType, "text") == 0) &&
                text[0] != '\0') {
                content += text;
            }
        }
    }

    return content.length() > 0 ? content : String("Error: no content in response");
}

String extractResponseId(const String &rawResponse) {
    StaticJsonDocument<32> filter;
    filter["id"] = true;

    DynamicJsonDocument doc(kResponseIdDocCapacity);
    const DeserializationError err =
        deserializeJson(doc, rawResponse, DeserializationOption::Filter(filter));
    if (err) {
        return "";
    }

    const char *id = doc["id"] | "";
    return String(id);
}

String extractErrorMessage(const String &rawResponse) {
    DynamicJsonDocument doc(rawResponse.length() + kErrorParseSlack);
    const DeserializationError err = deserializeJson(doc, rawResponse);
    if (err) {
        return rawResponse.substring(0, 200);
    }

    const char *errorMessage = doc["error"]["message"] | nullptr;
    if (errorMessage && errorMessage[0] != '\0') {
        return String(errorMessage);
    }

    const char *message = doc["message"] | nullptr;
    if (message && message[0] != '\0') {
        return String(message);
    }

    return rawResponse.substring(0, 200);
}

RequestResult runRequest(const String &body, const String &apiKey) {
    RequestResult result;

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(30);
    client.setTimeout(30);

    HTTPClient http;
    const String url = "https://" + String(OPENAI_HOST) + String(OPENAI_PATH);
    if (!http.begin(client, url)) {
        result.rawBody = "begin failed";
        return result;
    }

    http.setReuse(false);
    http.setConnectTimeout(kHttpConnectTimeoutMs);
    http.setTimeout(kHttpRequestTimeoutMs);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + apiKey);

    result.httpCode = http.POST(body);
    if (result.httpCode > 0) {
        result.rawBody = http.getString();
    } else {
        result.rawBody = http.errorToString(result.httpCode);
    }
    http.end();

    Serial.printf("API result code: %d\n", result.httpCode);
    return result;
}

RequestResult runRequestWithRetry(const String &body, const String &apiKey) {
    RequestResult result = runRequest(body, apiKey);
    if (result.httpCode > 0) {
        return result;
    }

    Serial.printf("Transport error %d. Retrying once...\n", result.httpCode);
    delay(kTransportRetryDelayMs);
    WifiManager::disconnect();
    delay(kWifiReconnectDelayMs);
    WifiManager::connect(kWifiReconnectTimeoutMs);
    ensureOpenAIHostResolves();
    return runRequest(body, apiKey);
}

}  // namespace

void clearConversation() {
    previousResponseId = "";
    Serial.println("Conversation cleared");
}

String chat(const String &userMessage,
            const String &imageBase64 = "",
            bool includeHistory = false,
            bool updateTextHistory = false) {
    if (!WifiManager::isConnected()) {
        return "Error: WiFi not connected";
    }

    const String apiKey = WifiManager::getAPIKey();
    if (apiKey.length() == 0) {
        return "Error: No API key configured";
    }

    RequestOptions options;
    options.userMessage = userMessage;
    options.imageBase64 = imageBase64;
    options.includePreviousResponse = includeHistory;
    options.updateConversationState = updateTextHistory;
    options.isVision = imageBase64.length() > 0;
    options.instructions = options.isVision ? CAMERA_PROMPT : SYSTEM_PROMPT;

    if (!ensureOpenAIHostResolves()) {
        return "DNS ERROR";
    }

    const String body = buildRequestBody(options);
    RequestResult result = runRequestWithRetry(body, apiKey);

    String response;
    if (result.httpCode == 200) {
        response = extractOutputText(result.rawBody);

        if (shouldStoreResponse(options)) {
            previousResponseId = extractResponseId(result.rawBody);
        }
    } else if (result.httpCode > 0) {
        response = "API error " + String(result.httpCode) + ": " +
                   extractErrorMessage(result.rawBody);
        Serial.println(response);
    } else {
        if (result.rawBody == "connection refused") {
            response = "DNS/NET ERROR";
        } else {
            response = "HTTP error " + String(result.httpCode) + ": " + result.rawBody;
        }
        Serial.println(response);
    }

    return response;
}

String ask(const String &prompt) {
    clearConversation();
    return chat(prompt, "", false, true);
}

String reply(const String &prompt) {
    return chat(prompt, "", true, true);
}

String solveImage(const String &imageBase64) {
    return chat("", imageBase64, false, false);
}

}  // namespace OpenAI
