// ══════════════════════════════════════════════════════════════
//  TI-84 AI Firmware v1.0
//  Custom firmware for XIAO ESP32S3 Sense inside a TI-84 Plus
// ══════════════════════════════════════════════════════════════
//
//  Commands (sent from calculator via Send({N})):
//    While locked:
//      Send({69420})  → Unlock
//      Send({any})    → Download TIAI program to calculator
//
//    While unlocked:
//      1 = Connect WiFi
//      2 = Disconnect WiFi
//      3 = Configure (start AP portal)
//      4 = Ask fresh (set pending text cmd; next Send(Str) is the prompt)
//      5 = Camera snap + solve (OpenAI vision)
//      6 = Scroll down
//      7 = Scroll up
//      8 = Reply with context
//
// ══════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <algorithm>
#include <CBL2.h>
#include <TIVar.h>
#include "config.h"
#include "camera.h"
#include "wifi_manager.h"
#include "openai_client.h"
#include "program_data.h"

// ── CBL2 link layer ──
CBL2 cbl;
static const int MAX_DATA_LEN = 4096;
static uint8_t header[16];
static uint8_t data[MAX_DATA_LEN];

// ── State ──
static bool unlocked = false;
static bool unlockArmed = false;
static bool sendProgramFlag = false;
static bool startConfigFlag = false;
static bool connectFlag = false;
static int pendingTextCmd = 0;  // 0=none, 4=fresh ask, 8=reply
static unsigned long lastHeartbeat = 0;
static unsigned long sendProgramAt = 0;

// ── Pagination ──
static String fullResponse = "";
static int currentPage = 0;
static int totalPages = 0;
static String responsePages[72];

// ── Response queue ──
static String responseStr = "";
static bool responseReady = false;

// ══════════════════════════════════════════════════════════════
//  Send TIAI program to calculator via raw TICL silent link
// ══════════════════════════════════════════════════════════════

void sendProgramVariable() {
    Serial.println("Sending TIAI program to calculator...");

    uint16_t contentLen = PROGRAM_DATA_LEN;
    uint16_t dataLen = contentLen + 2;

    // RTS header: [size_lo, size_hi, type, name(8), version, flags]
    uint8_t rts[13];
    rts[0] = dataLen & 0xFF;
    rts[1] = (dataLen >> 8) & 0xFF;
    rts[2] = 0x05;  // Type: Program
    memset(&rts[3], 0x00, 8);
    memcpy(&rts[3], PROGRAM_NAME, strlen(PROGRAM_NAME));
    rts[11] = 0x00;  // Version
    rts[12] = 0x00;  // Flags

    // DATA payload: [content_len(2), tokenized_content...]
    uint8_t *payload = (uint8_t *)malloc(dataLen);
    if (!payload) { Serial.println("malloc failed"); return; }
    payload[0] = contentLen & 0xFF;
    payload[1] = (contentLen >> 8) & 0xFF;
    memcpy(&payload[2], PROGRAM_DATA, contentLen);

    uint8_t txH[4], rxH[4], rxBuf[64];
    int rxLen;

    cbl.resetLines();

    // RTS
    txH[0] = 0x23; txH[1] = 0xC9;
    txH[2] = 13; txH[3] = 0;
    if (cbl.send(txH, rts, 13)) { Serial.println("RTS failed"); free(payload); return; }

    // Wait ACK
    if (cbl.get(rxH, rxBuf, &rxLen, 64) || rxH[1] != 0x56) {
        Serial.println("No ACK after RTS"); free(payload); return;
    }

    // Wait CTS
    if (cbl.get(rxH, rxBuf, &rxLen, 64)) {
        Serial.println("No CTS"); free(payload); return;
    }
    if (rxH[1] == 0x36) {
        Serial.println("Calculator rejected transfer"); free(payload); return;
    }

    // ACK the CTS
    if (rxH[1] == 0x09) {
        txH[1] = 0x56; txH[2] = 0; txH[3] = 0;
        cbl.send(txH, nullptr, 0);
    }

    // DATA
    txH[0] = 0x23; txH[1] = 0x15;
    txH[2] = dataLen & 0xFF; txH[3] = (dataLen >> 8) & 0xFF;
    if (cbl.send(txH, payload, dataLen)) {
        Serial.println("DATA failed"); free(payload); return;
    }
    free(payload);

    // Wait ACK
    if (cbl.get(rxH, rxBuf, &rxLen, 64) || rxH[1] != 0x56) {
        Serial.println("No ACK after DATA"); return;
    }

    // EOT
    txH[1] = 0x92; txH[2] = 0; txH[3] = 0;
    if (cbl.send(txH, nullptr, 0)) {
        Serial.println("EOT failed");
        return;
    }

    // Wait final ACK so the calculator closes the transfer cleanly.
    if (cbl.get(rxH, rxBuf, &rxLen, 64, 5000000) || rxH[1] != 0x56) {
        Serial.println("No ACK after EOT");
        return;
    }

    Serial.println("Program sent OK!");
}

// ══════════════════════════════════════════════════════════════
//  Pagination
// ══════════════════════════════════════════════════════════════

void setResponse(const String &text) {
    fullResponse = text;
    currentPage = 0;
    totalPages = 0;

    for (int i = 0; i < 72; i++) {
        responsePages[i] = "                ";
    }

    auto addLine = [&](const String &rawLine) {
        if (totalPages >= 72) return;
        String padded = rawLine;
        if (padded.length() > SCREEN_WIDTH) padded = padded.substring(0, SCREEN_WIDTH);
        while (padded.length() < SCREEN_WIDTH) padded += ' ';
        responsePages[totalPages++] = padded;
    };

    auto wrapParagraph = [&](String paragraph) {
        if (totalPages >= 72) return;

        paragraph.replace('\t', ' ');
        paragraph.trim();

        if (paragraph.length() == 0) {
            addLine("");
            return;
        }

        String line = "";
        String word = "";

        auto emitWord = [&](String rawWord) {
            if (totalPages >= 72) return;

            while (rawWord.length() > SCREEN_WIDTH) {
                if (line.length() > 0) {
                    addLine(line);
                    line = "";
                    if (totalPages >= 72) return;
                }
                addLine(rawWord.substring(0, SCREEN_WIDTH));
                rawWord = rawWord.substring(SCREEN_WIDTH);
                if (totalPages >= 72) return;
            }

            if (rawWord.length() == 0) return;

            if (line.length() == 0) {
                line = rawWord;
            } else if (line.length() + 1 + rawWord.length() <= SCREEN_WIDTH) {
                line += " " + rawWord;
            } else {
                addLine(line);
                line = rawWord;
            }
        };

        for (unsigned int i = 0; i <= paragraph.length(); i++) {
            char c = (i < paragraph.length()) ? paragraph[i] : ' ';
            if (c == ' ') {
                if (word.length() > 0) {
                    emitWord(word);
                    word = "";
                    if (totalPages >= 72) break;
                }
            } else {
                word += c;
            }
        }

        if (line.length() > 0 && totalPages < 72) {
            addLine(line);
        }
    };

    String paragraph = "";
    for (unsigned int i = 0; i < text.length(); i++) {
        char c = text[i];
        if (c == '\r') continue;
        if (c == '\n') {
            wrapParagraph(paragraph);
            paragraph = "";
            continue;
        }
        paragraph += c;
    }
    wrapParagraph(paragraph);

    if (totalPages == 0) {
        addLine("");
    }
}

String getPage(int page) {
    if (totalPages <= 0) return "                                                ";
    if (page < 0) page = 0;

    int maxTopLine = std::max(0, totalPages - 3);
    if (page > maxTopLine) page = maxTopLine;

    String window = "";
    for (int i = 0; i < 3; i++) {
        int idx = page + i;
        if (idx >= 0 && idx < totalPages) {
            window += responsePages[idx];
        } else {
            window += "                ";
        }
    }
    return window;
}

void queueResponse(const String &s) { responseStr = s; responseReady = true; }
void queueError(const String &s) { responseStr = "ERR:" + s; responseReady = true; }

void scheduleProgramSend() {
    // Wait a short moment so the calculator can finish the EOT/ACK
    // that closes its original Send() exchange before we start ours.
    sendProgramFlag = true;
    sendProgramAt = millis() + 150;
}

// ══════════════════════════════════════════════════════════════
//  Command handlers
// ══════════════════════════════════════════════════════════════

// 1: Connect WiFi
void cmdConnect() {
    if (WifiManager::isConnected()) {
        queueResponse("CONNECTED");
    } else {
        connectFlag = true;
        Serial.println("Queued WiFi connect");
    }
}

// 2: Disconnect WiFi
void cmdDisconnect() {
    WifiManager::disconnect();
    queueResponse("DISCONNECTED");
}

// 3: Configure — start AP portal (handled in loop after response is sent)
void cmdConfigure() {
    queueResponse("AP STARTING...");
    startConfigFlag = true;
}

// 5: Camera snap + solve
void cmdSnapSolve() {
    if (!WifiManager::isConnected()) { queueError("NO WIFI"); return; }
    String b64 = Camera::captureBase64();
    if (b64.length() == 0) { queueError("CAMERA FAIL"); return; }
    Serial.println("Sending to OpenAI vision...");
    String result = OpenAI::solveImage(b64);
    setResponse(result);
    queueResponse(getPage(0));
}

// 6: Scroll down one line
void cmdNextPage() {
    if (totalPages <= 0) { queueError("NO RESPONSE"); return; }
    int maxTopLine = std::max(0, totalPages - 3);
    if (currentPage < maxTopLine) currentPage++;
    queueResponse(getPage(currentPage));
}

// 7: Scroll up one line
void cmdPrevPage() {
    if (totalPages <= 0) { queueError("NO RESPONSE"); return; }
    currentPage--;
    if (currentPage < 0) currentPage = 0;
    queueResponse(getPage(currentPage));
}

// Handle text prompt (received after Send({4}) or Send({8}))
void handleTextPrompt(const String &text, bool isReply) {
    if (!WifiManager::isConnected()) { queueError("NO WIFI"); return; }
    if (text.length() == 0) { queueError("EMPTY PROMPT"); return; }
    Serial.printf("%s: %s\n", isReply ? "Reply" : "Prompt", text.c_str());
    String result = isReply ? OpenAI::reply(text) : OpenAI::ask(text);
    setResponse(result);
    queueResponse(getPage(0));
}

// ══════════════════════════════════════════════════════════════
//  CBL2 callbacks
// ══════════════════════════════════════════════════════════════

int onReceived(uint8_t type, enum Endpoint model, int datalen) {
    Serial.printf("RX: type=0x%02X len=%d\n", type, datalen);

    // ── Real number ──
    if (type == 0x00) {
        long val = (long)TIVar::realToFloat8x(data, model);

        if (!unlocked) {
            if (val == PASSWORD) {
                unlocked = true;
                unlockArmed = true;
                pendingTextCmd = 0;
                responseStr = "";
                responseReady = false;
            } else {
                // Any non-password Send while locked → download program
                scheduleProgramSend();
            }
            return 0;
        }

        if (val == PASSWORD) {
            unlockArmed = true;
            pendingTextCmd = 0;
            responseStr = "";
            responseReady = false;
            connectFlag = false;
            return 0;
        }

        if (!unlockArmed) {
            Serial.printf("Numeric command %ld without fresh unlock -> sending program\n", val);
            scheduleProgramSend();
            return 0;
        }
        unlockArmed = false;

        switch (val) {
            case 1: cmdConnect();    break;
            case 2: cmdDisconnect(); break;
            case 3: cmdConfigure();  break;
            case 4:
                pendingTextCmd = 4;
                responseStr = "";
                responseReady = false;
                break;
            case 8:
                pendingTextCmd = 8;
                responseStr = "";
                responseReady = false;
                break;
            case 5: cmdSnapSolve();  break;
            case 6: cmdNextPage();   break;
            case 7: cmdPrevPage();   break;
            default: queueError("UNKNOWN CMD"); break;
        }
    }
    // ── Real list ──
    else if (type == 0x01) {
        int listSize = TIVar::sizeWordToInt(&data[0]);
        if (listSize < 1) return 0;
        long val = (long)TIVar::realToFloat8x(&data[2], model);

        if (!unlocked) {
            if (val == PASSWORD) {
                unlocked = true;
                unlockArmed = true;
                pendingTextCmd = 0;
                responseStr = "";
                responseReady = false;
            } else {
                scheduleProgramSend();
            }
            return 0;
        }

        if (val == PASSWORD) {
            unlockArmed = true;
            pendingTextCmd = 0;
            responseStr = "";
            responseReady = false;
            connectFlag = false;
            return 0;
        }

        if (!unlockArmed) {
            Serial.printf("List command %ld without fresh unlock -> sending program\n", val);
            scheduleProgramSend();
            return 0;
        }
        unlockArmed = false;

        switch (val) {
            case 1: cmdConnect();    break;
            case 2: cmdDisconnect(); break;
            case 3: cmdConfigure();  break;
            case 4:
                pendingTextCmd = 4;
                responseStr = "";
                responseReady = false;
                break;
            case 8:
                pendingTextCmd = 8;
                responseStr = "";
                responseReady = false;
                break;
            case 5: cmdSnapSolve();  break;
            case 6: cmdNextPage();   break;
            case 7: cmdPrevPage();   break;
            default: queueError("UNKNOWN CMD"); break;
        }
    }
    // ── String ──
    else if (type == 0x04) {
        String text = TIVar::strVarToString8x(data, model);

        if (!unlocked) { scheduleProgramSend(); return 0; }

        if (pendingTextCmd == 4 || pendingTextCmd == 8) {
            bool isReply = (pendingTextCmd == 8);
            pendingTextCmd = 0;
            handleTextPrompt(text, isReply);
        } else {
            // Default: treat any string as a question
            handleTextPrompt(text, false);
        }
    }

    return 0;
}

int onRequest(uint8_t type, enum Endpoint model, int* headerLen, int* dataLen, data_callback* dcb) {
    if (type != VarTypes82::VarString) {
        Serial.printf("REQ: unsupported type=0x%02X\n", type);
        return -1;
    }

    String toSend = responseReady ? responseStr : "READY";
    responseReady = false;
    responseStr = "";

    int len = TIVar::stringToStrVar8x(toSend.c_str(), data, model);
    if (len < 0) {
        Serial.println("REQ: failed to encode string response");
        return -1;
    }

    memset(header, 0, sizeof(header));
    TIVar::intToSizeWord(len, header);
    header[2] = VarTypes82::VarString;
    header[3] = 0xAA;  // String variable token prefix.
    header[4] = 0x00;  // String slot token used by ArTICL's Get(StrX) examples.
    *headerLen = 13;
    *dataLen = len;
    *dcb = nullptr;

    Serial.printf("REQ: send=\"%s\" len=%d\n", toSend.c_str(), len);
    return 0;
}

// ══════════════════════════════════════════════════════════════
//  Setup & Loop
// ══════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    delay(3000);  // Extra time for USB CDC to enumerate
    Serial.println("\n=== TI-84 AI Firmware v1.0 ===");
    Serial.printf("PIN_TIP=%d, PIN_RING=%d\n", PIN_TIP, PIN_RING);
    Serial.flush();

    Serial.println("Initializing camera...");
    Camera::init();
    Serial.println("Loading credentials...");
    WifiManager::loadCredentials();

    if (!WifiManager::hasCredentials()) {
        Serial.println("No credentials. Starting config portal + CBL2...");
        WifiManager::startConfigPortal();
    } else {
        WifiManager::connect();
    }

    // ALWAYS start CBL2 — even during first-boot config portal
    cbl.setLines(PIN_TIP, PIN_RING);
    cbl.resetLines();
    cbl.setupCallbacks(header, data, MAX_DATA_LEN, onReceived, onRequest);

    Serial.println("Ready. Waiting for calculator...");
}

void loop() {
    cbl.eventLoopTick(true);

    // Heartbeat every 10 seconds
    unsigned long now = millis();
    if (now - lastHeartbeat >= 10000) {
        lastHeartbeat = now;
        Serial.printf("[%lus] alive, unlocked=%d\n", now/1000, unlocked);
    }

    // Handle config portal web server (non-blocking)
    WifiManager::handleConfigPortal();

    // Run WiFi connect outside the link receive callback so Get(Str0)
    // can keep polling until the final result is ready.
    if (connectFlag) {
        connectFlag = false;
        Serial.println("Running queued WiFi connect...");
        if (WifiManager::isConnected() || WifiManager::connect()) {
            queueResponse("CONNECTED");
        } else {
            queueError("WIFI FAILED");
        }
    }

    // Download program to calculator (after CBL2 handshake completes)
    if (sendProgramFlag && millis() >= sendProgramAt) {
        sendProgramFlag = false;
        Serial.println(">>> sendProgramFlag! Sending program...");
        sendProgramVariable();
    }

    // Start config portal (after response to calculator is sent)
    if (startConfigFlag && !responseReady) {
        startConfigFlag = false;
        WifiManager::startConfigPortal();
    }
}
