#include <Arduino.h>
#include <algorithm>
#include <CBL2.h>
#include <TIVar.h>

#include "camera.h"
#include "config.h"
#include "openai_client.h"
#include "program_data.h"
#include "wifi_manager.h"

namespace {

constexpr int kMaxDataLen = 4096;
constexpr int kHeaderBufferLen = 16;
constexpr int kMaxResponseLines = 72;
constexpr int kResponseWindowLines = 5;
constexpr unsigned long kHeartbeatIntervalMs = 10000;
constexpr unsigned long kProgramSendDelayMs = 300;

enum class PendingTextCommand : uint8_t {
    None = 0,
    AskFresh = 4,
    Reply = 8,
};

enum CommandId : long {
    kCommandConnect = 1,
    kCommandDisconnect = 2,
    kCommandConfigure = 3,
    kCommandAskFresh = 4,
    kCommandSnapSolve = 5,
    kCommandScrollDown = 6,
    kCommandScrollUp = 7,
    kCommandReply = 8,
    kCommandShowPreviousResponse = 9,
};

CBL2 cbl;
uint8_t header[kHeaderBufferLen];
uint8_t data[kMaxDataLen];

bool unlocked = false;
bool unlockArmed = false;
bool sendProgramFlag = false;
bool startConfigFlag = false;
bool connectFlag = false;
PendingTextCommand pendingTextCommand = PendingTextCommand::None;
unsigned long lastHeartbeat = 0;
unsigned long sendProgramAt = 0;

int currentTopLine = 0;
int totalResponseLines = 0;
String responseLines[kMaxResponseLines];
int previousTopLine = 0;
int previousTotalResponseLines = 0;
String previousResponseLines[kMaxResponseLines];
bool previousResponseAvailable = false;
String previousConversationResponseId = "";
String queuedResponse = "";
bool responseReady = false;

const String &blankResponseLine() {
    static const String line = []() {
        String value;
        value.reserve(SCREEN_WIDTH);
        for (int i = 0; i < SCREEN_WIDTH; i++) {
            value += ' ';
        }
        return value;
    }();
    return line;
}

const String &blankResponseWindow() {
    static const String window = []() {
        String value;
        value.reserve(kResponseWindowLines * SCREEN_WIDTH);
        for (int i = 0; i < kResponseWindowLines; i++) {
            value += blankResponseLine();
        }
        return value;
    }();
    return window;
}

String padDisplayLine(String value) {
    if (value.length() > SCREEN_WIDTH) {
        value = value.substring(0, SCREEN_WIDTH);
    }
    while (value.length() < SCREEN_WIDTH) {
        value += ' ';
    }
    return value;
}

void clearQueuedResponse() {
    queuedResponse = "";
    responseReady = false;
}

void resetPendingTextCommand() {
    pendingTextCommand = PendingTextCommand::None;
}

void resetUnlockedSessionState() {
    resetPendingTextCommand();
    clearQueuedResponse();
    connectFlag = false;
}

void swapDisplayLineArrays(String *lhs, String *rhs) {
    for (int i = 0; i < kMaxResponseLines; i++) {
        String temp = lhs[i];
        lhs[i] = rhs[i];
        rhs[i] = temp;
    }
}

void saveCurrentResponseAsPrevious() {
    previousTopLine = currentTopLine;
    previousTotalResponseLines = totalResponseLines;
    for (int i = 0; i < kMaxResponseLines; i++) {
        previousResponseLines[i] = responseLines[i];
    }
    previousConversationResponseId = OpenAI::conversationResponseId();
    previousResponseAvailable = previousTotalResponseLines > 0;
}

void acceptUnlock() {
    unlocked = true;
    unlockArmed = true;
    resetUnlockedSessionState();
}

void refreshUnlockWindow() {
    unlockArmed = true;
    resetUnlockedSessionState();
}

void queueResponse(const String &value) {
    queuedResponse = value;
    responseReady = true;
}

void queueError(const String &value) {
    queueResponse("ERR:" + value);
}

void scheduleProgramSend() {
    sendProgramFlag = true;
    sendProgramAt = millis() + kProgramSendDelayMs;
}

bool sendProgramVariableOnce() {
    Serial.println("Sending TIAI program to calculator...");

    const uint16_t contentLen = PROGRAM_DATA_LEN;
    const uint16_t dataLen = contentLen + 2;

    uint8_t rts[13];
    rts[0] = dataLen & 0xFF;
    rts[1] = (dataLen >> 8) & 0xFF;
    rts[2] = 0x05;
    memset(&rts[3], 0x00, 8);
    memcpy(&rts[3], PROGRAM_NAME, strlen(PROGRAM_NAME));
    rts[11] = 0x00;
    rts[12] = 0x00;

    uint8_t *payload = static_cast<uint8_t *>(malloc(dataLen));
    if (!payload) {
        Serial.println("malloc failed");
        return false;
    }

    payload[0] = contentLen & 0xFF;
    payload[1] = (contentLen >> 8) & 0xFF;
    memcpy(&payload[2], PROGRAM_DATA, contentLen);

    uint8_t txHeader[4];
    uint8_t rxHeader[4];
    uint8_t rxBuffer[64];
    int rxLen = 0;

    cbl.resetLines();

    txHeader[0] = 0x23;
    txHeader[1] = 0xC9;
    txHeader[2] = 13;
    txHeader[3] = 0;
    if (cbl.send(txHeader, rts, 13)) {
        Serial.println("RTS failed");
        free(payload);
        return false;
    }

    if (cbl.get(rxHeader, rxBuffer, &rxLen, 64) || rxHeader[1] != 0x56) {
        Serial.println("No ACK after RTS");
        free(payload);
        return false;
    }

    if (cbl.get(rxHeader, rxBuffer, &rxLen, 64)) {
        Serial.println("No CTS");
        free(payload);
        return false;
    }

    if (rxHeader[1] == 0x36) {
        Serial.println("Calculator rejected transfer");
        free(payload);
        return false;
    }

    if (rxHeader[1] == 0x09) {
        txHeader[1] = 0x56;
        txHeader[2] = 0;
        txHeader[3] = 0;
        cbl.send(txHeader, nullptr, 0);
    }

    txHeader[0] = 0x23;
    txHeader[1] = 0x15;
    txHeader[2] = dataLen & 0xFF;
    txHeader[3] = (dataLen >> 8) & 0xFF;
    if (cbl.send(txHeader, payload, dataLen)) {
        Serial.println("DATA failed");
        free(payload);
        return false;
    }
    free(payload);

    if (cbl.get(rxHeader, rxBuffer, &rxLen, 64) || rxHeader[1] != 0x56) {
        Serial.println("No ACK after DATA");
        return false;
    }

    txHeader[1] = 0x92;
    txHeader[2] = 0;
    txHeader[3] = 0;
    if (cbl.send(txHeader, nullptr, 0)) {
        Serial.println("EOT failed");
        return false;
    }

    if (cbl.get(rxHeader, rxBuffer, &rxLen, 64, 5000000) || rxHeader[1] != 0x56) {
        Serial.println("No ACK after EOT (treating as success)");
        return true;
    }

    Serial.println("Program sent OK!");
    return true;
}

void sendProgramVariable() {
    if (sendProgramVariableOnce()) {
        return;
    }

    Serial.println("Program send failed; retrying once...");
    delay(250);
    cbl.resetLines();

    if (!sendProgramVariableOnce()) {
        Serial.println("Program send failed after retry");
    }
}

void clearResponseLines() {
    for (String &line : responseLines) {
        line = blankResponseLine();
    }
    totalResponseLines = 0;
    currentTopLine = 0;
}

void addResponseLine(const String &rawLine) {
    if (totalResponseLines >= kMaxResponseLines) {
        return;
    }

    responseLines[totalResponseLines++] = padDisplayLine(rawLine);
}

void wrapResponseParagraph(String paragraph) {
    if (totalResponseLines >= kMaxResponseLines) {
        return;
    }

    paragraph.replace('\t', ' ');
    paragraph.trim();
    if (paragraph.length() == 0) {
        addResponseLine("");
        return;
    }

    String line = "";
    String word = "";

    auto emitWord = [&](String rawWord) {
        if (totalResponseLines >= kMaxResponseLines) {
            return;
        }

        while (rawWord.length() > SCREEN_WIDTH) {
            if (line.length() > 0) {
                addResponseLine(line);
                line = "";
                if (totalResponseLines >= kMaxResponseLines) {
                    return;
                }
            }
            addResponseLine(rawWord.substring(0, SCREEN_WIDTH));
            rawWord = rawWord.substring(SCREEN_WIDTH);
            if (totalResponseLines >= kMaxResponseLines) {
                return;
            }
        }

        if (rawWord.length() == 0) {
            return;
        }

        if (line.length() == 0) {
            line = rawWord;
        } else if (line.length() + 1 + rawWord.length() <= SCREEN_WIDTH) {
            line += " " + rawWord;
        } else {
            addResponseLine(line);
            line = rawWord;
        }
    };

    for (unsigned int i = 0; i <= paragraph.length(); i++) {
        char c = (i < paragraph.length()) ? paragraph[i] : ' ';
        if (c == ' ') {
            if (word.length() > 0) {
                emitWord(word);
                word = "";
                if (totalResponseLines >= kMaxResponseLines) {
                    break;
                }
            }
        } else {
            word += c;
        }
    }

    if (line.length() > 0 && totalResponseLines < kMaxResponseLines) {
        addResponseLine(line);
    }
}

void setResponse(const String &text) {
    clearResponseLines();

    String paragraph = "";
    for (unsigned int i = 0; i < text.length(); i++) {
        char c = text[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            wrapResponseParagraph(paragraph);
            paragraph = "";
            continue;
        }
        paragraph += c;
    }
    wrapResponseParagraph(paragraph);

    if (totalResponseLines == 0) {
        addResponseLine("");
    }
}

int maxTopLine() {
    return std::max(0, totalResponseLines - kResponseWindowLines);
}

String getPageWindow(int topLine) {
    if (totalResponseLines <= 0) {
        return blankResponseWindow();
    }

    topLine = std::max(0, std::min(topLine, maxTopLine()));

    String window = "";
    for (int i = 0; i < kResponseWindowLines; i++) {
        const int lineIndex = topLine + i;
        if (lineIndex >= 0 && lineIndex < totalResponseLines) {
            window += responseLines[lineIndex];
        } else {
            window += blankResponseLine();
        }
    }
    return window;
}

void queueCurrentWindow() {
    queueResponse(getPageWindow(currentTopLine));
}

void restorePreviousResponse() {
    if (!previousResponseAvailable) {
        queueError("NO PREVIOUS");
        return;
    }

    std::swap(currentTopLine, previousTopLine);
    std::swap(totalResponseLines, previousTotalResponseLines);
    swapDisplayLineArrays(responseLines, previousResponseLines);

    const String currentConversationResponseId = OpenAI::conversationResponseId();
    OpenAI::setConversationResponseId(previousConversationResponseId);
    previousConversationResponseId = currentConversationResponseId;
    previousResponseAvailable = previousTotalResponseLines > 0;

    queueCurrentWindow();
}

void armPendingTextCommand(PendingTextCommand command) {
    pendingTextCommand = command;
    clearQueuedResponse();
}

void cmdConnect() {
    if (WifiManager::isConnected()) {
        queueResponse("CONNECTED");
        return;
    }

    connectFlag = true;
    Serial.println("Queued WiFi connect");
}

void cmdDisconnect() {
    WifiManager::disconnect();
    queueResponse("DISCONNECTED");
}

void cmdConfigure() {
    queueResponse("AP STARTING...");
    startConfigFlag = true;
}

void cmdSnapSolve() {
    if (!WifiManager::isConnected()) {
        queueError("NO WIFI");
        return;
    }

    String imageBase64 = Camera::captureBase64();
    if (imageBase64.length() == 0) {
        queueError("CAMERA FAIL");
        return;
    }

    Serial.println("Sending to OpenAI vision...");
    setResponse(OpenAI::solveImage(imageBase64));
    queueCurrentWindow();
}

void cmdScrollDown() {
    if (totalResponseLines <= 0) {
        queueError("NO RESPONSE");
        return;
    }

    if (currentTopLine < maxTopLine()) {
        currentTopLine++;
    }
    queueCurrentWindow();
}

void cmdScrollUp() {
    if (totalResponseLines <= 0) {
        queueError("NO RESPONSE");
        return;
    }

    if (currentTopLine > 0) {
        currentTopLine--;
    }
    queueCurrentWindow();
}

void handleTextPrompt(const String &text, bool isReply) {
    if (!WifiManager::isConnected()) {
        queueError("NO WIFI");
        return;
    }
    if (text.length() == 0) {
        queueError("EMPTY PROMPT");
        return;
    }

    Serial.printf("%s received (%u chars)\n",
                  isReply ? "Reply" : "Prompt",
                  text.length());
    if (isReply) {
        saveCurrentResponseAsPrevious();
    }
    setResponse(isReply ? OpenAI::reply(text) : OpenAI::ask(text));
    queueCurrentWindow();
}

void dispatchUnlockedCommand(long value) {
    switch (value) {
        case kCommandConnect:
            cmdConnect();
            break;
        case kCommandDisconnect:
            cmdDisconnect();
            break;
        case kCommandConfigure:
            cmdConfigure();
            break;
        case kCommandAskFresh:
            armPendingTextCommand(PendingTextCommand::AskFresh);
            break;
        case kCommandSnapSolve:
            cmdSnapSolve();
            break;
        case kCommandScrollDown:
            cmdScrollDown();
            break;
        case kCommandScrollUp:
            cmdScrollUp();
            break;
        case kCommandReply:
            armPendingTextCommand(PendingTextCommand::Reply);
            break;
        case kCommandShowPreviousResponse:
            restorePreviousResponse();
            break;
        default:
            queueError("UNKNOWN CMD");
            break;
    }
}

void handleInboundCommandValue(long value, const char *sourceLabel) {
    if (!unlocked) {
        if (value == PASSWORD) {
            acceptUnlock();
        } else {
            scheduleProgramSend();
        }
        return;
    }

    if (value == PASSWORD) {
        refreshUnlockWindow();
        return;
    }

    if (!unlockArmed) {
        Serial.printf("%s command %ld without fresh unlock -> sending program\n",
                      sourceLabel, value);
        scheduleProgramSend();
        return;
    }

    unlockArmed = false;
    dispatchUnlockedCommand(value);
}

}  // namespace

int onReceived(uint8_t type, enum Endpoint model, int datalen) {
    Serial.printf("RX: type=0x%02X len=%d\n", type, datalen);

    if (type == 0x00) {
        const long value = static_cast<long>(TIVar::realToFloat8x(data, model));
        handleInboundCommandValue(value, "Numeric");
        return 0;
    }

    if (type == 0x01) {
        const int listSize = TIVar::sizeWordToInt(&data[0]);
        if (listSize < 1) {
            return 0;
        }
        const long value = static_cast<long>(TIVar::realToFloat8x(&data[2], model));
        handleInboundCommandValue(value, "List");
        return 0;
    }

    if (type == 0x04) {
        const String text = TIVar::strVarToString8x(data, model);
        if (!unlocked) {
            scheduleProgramSend();
            return 0;
        }

        const PendingTextCommand command = pendingTextCommand;
        resetPendingTextCommand();

        if (command == PendingTextCommand::Reply) {
            handleTextPrompt(text, true);
        } else {
            handleTextPrompt(text, false);
        }
    }

    return 0;
}

int onRequest(uint8_t type, enum Endpoint model, int *headerLen, int *dataLen, data_callback *dcb) {
    if (type != VarTypes82::VarString) {
        Serial.printf("REQ: unsupported type=0x%02X\n", type);
        return -1;
    }

    if (!responseReady) {
        queueResponse("READY");
    }

    const String toSend = queuedResponse;
    clearQueuedResponse();

    const int len = TIVar::stringToStrVar8x(toSend.c_str(), data, model);
    if (len < 0) {
        Serial.println("REQ: failed to encode string response");
        return -1;
    }

    memset(header, 0, sizeof(header));
    TIVar::intToSizeWord(len, header);
    header[2] = VarTypes82::VarString;
    header[3] = 0xAA;
    header[4] = 0x00;
    *headerLen = 13;
    *dataLen = len;
    *dcb = nullptr;

    Serial.printf("REQ: send len=%d\n", len);
    return 0;
}

void setup() {
    Serial.begin(115200);
    delay(3000);
    Serial.println("\n=== TI-84 AI Firmware ===");
    Serial.printf("PIN_TIP=%d, PIN_RING=%d\n", PIN_TIP, PIN_RING);
    Serial.flush();

    Serial.println("Initializing camera...");
    Camera::init();

    Serial.println("Loading credentials...");
    WifiManager::loadCredentials();
    Camera::applyQualityProfile(WifiManager::cameraQualityProfile());

    cbl.setLines(PIN_TIP, PIN_RING);
    cbl.resetLines();
    cbl.setupCallbacks(header, data, kMaxDataLen, onReceived, onRequest);

    if (!WifiManager::hasCredentials()) {
        Serial.println("No credentials. Starting config portal...");
        WifiManager::startConfigPortal();
    } else {
        Serial.println("Credentials loaded. Use calculator CONNECT to join WiFi.");
    }

    Serial.println("Ready. Waiting for calculator...");
}

void loop() {
    cbl.eventLoopTick(true);

    const unsigned long now = millis();
    if (now - lastHeartbeat >= kHeartbeatIntervalMs) {
        lastHeartbeat = now;
        Serial.printf("[%lus] alive, unlocked=%d\n", now / 1000, unlocked);
    }

    WifiManager::handleConfigPortal();

    if (connectFlag) {
        connectFlag = false;
        Serial.println("Running queued WiFi connect...");
        if (WifiManager::isConnected() || WifiManager::connect()) {
            queueResponse("CONNECTED");
        } else {
            queueError("WIFI FAILED");
        }
    }

    if (sendProgramFlag && millis() >= sendProgramAt) {
        sendProgramFlag = false;
        Serial.println(">>> sendProgramFlag! Sending program...");
        sendProgramVariable();
    }

    if (startConfigFlag && !responseReady) {
        startConfigFlag = false;
        WifiManager::startConfigPortal();
    }
}
