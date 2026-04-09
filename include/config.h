#pragma once

#include <stdint.h>

// Link port pins (2.5mm jack)
#define PIN_TIP   D0
#define PIN_RING  D1

// Authentication
#define PASSWORD  69420

// Display and pagination
#define SCREEN_WIDTH  16

// Camera
#define CAM_XCLK_FREQ      20000000
#define CAM_JPEG_QUALITY   7
#define CAM_FRAMESIZE      FRAMESIZE_SXGA
#define CAM_PROFILE_LOW       0
#define CAM_PROFILE_BALANCED  1
#define CAM_PROFILE_HIGH      2
#define CAM_PROFILE_DEFAULT   CAM_PROFILE_BALANCED

// OpenAI
#define OPENAI_HOST   "api.openai.com"
#define OPENAI_PATH   "/v1/responses"
#define OPENAI_MODEL  "gpt-5.4"

// WiFi AP portals
// Keep these TI-friendly so the calculator can display them cleanly.
#define AP_SSID        "TI84AI"
#define AP_PASS        "12345678"
#define AP_IP          IPAddress(192, 168, 4, 1)

#define DEBUG_AP_SSID  "TI84CAM"
#define DEBUG_AP_PASS  "12345678"
#define DEBUG_AP_IP    IPAddress(192, 168, 8, 1)

// NVS storage keys
#define NVS_NAMESPACE  "ti84cfg"
#define NVS_KEY_SSID   "wifi_ssid"
#define NVS_KEY_PASS   "wifi_pass"
#define NVS_KEY_APIKEY "api_key"
#define NVS_KEY_DEBUGAP "debug_ap"
#define NVS_KEY_CAMPROF "cam_prof"
#define NVS_KEY_PHOTORECAP "photo_recap"
#define DEFAULT_DEBUG_AP_ENABLED true
#define DEFAULT_PHOTO_RECAP_ENABLED false

static inline uint8_t normalizeCameraProfileValue(uint8_t profile) {
    switch (profile) {
        case CAM_PROFILE_LOW:
        case CAM_PROFILE_BALANCED:
        case CAM_PROFILE_HIGH:
            return profile;
        default:
            return CAM_PROFILE_DEFAULT;
    }
}

// Model prompts
#define SYSTEM_PROMPT \
    "You are an assistant inside a TI-84 Plus calculator. " \
    "The screen is 16 characters wide and 8 rows tall. " \
    "You help solve math, physics, chemistry, engineering, and academic problems. " \
    "Rules:\n" \
    "- Start with answers only. No explanation yet.\n" \
    "- If there are multiple answers, list them in left-to-right visual order.\n" \
    "- For charts, graphs, tables, labels, registers, offsets, memory rows, or multiple blanks, read left-to-right and give answers in that order.\n" \
    "- Name where each answer goes. Do not give unlabeled value dumps.\n" \
    "- For tables or fill-in blanks, use one labeled answer per line.\n" \
    "- For tables, output cells in top-to-bottom row order and left-to-right column order.\n" \
    "- Preserve exact visible labels when readable. Do not rename labels like mul3, eax, before, or after.\n" \
    "- If a label is unclear, briefly say the label is unclear, then use your best reading.\n" \
    "- Example format: mul3 +3 before = 00 or eax after = 00000024.\n" \
    "- If a graph would help, describe it in words using key points, intercepts, slope, direction, max/min, and where values go. Do not try to draw a graph.\n" \
    "- After all answers, provide the work.\n" \
    "- For table-fill questions, keep the work very short after the filled entries.\n" \
    "- Use short lines and newline breaks.\n" \
    "- Plaintext only. No markdown, no bullets, no code blocks, no LaTeX, no special formatting.\n" \
    "- ASCII math: ^ exponents, * multiply, / divide, sqrt() roots.\n" \
    "- Never use these characters: # $ % & ; @ _ ` | ~\n" \
    "- For multiple choice, give just the letter answer first, then the work.\n" \
    "- Prefer exact values over decimals.\n" \
    "- Keep total response under 400 characters when possible.\n" \
    "- End the final line exactly with: END OF MESSAGE."

#define CAMERA_PROMPT \
    "Read the photo and try your best. " \
    "Only refuse if it is truly unreadable. " \
    "If partly unclear, briefly say what is unclear and solve what is readable. " \
    "If photo quality hurts accuracy, briefly say how to improve it. " \
    "Start with answers only. " \
    "For multiple answers, use left-to-right order. " \
    "Name where each answer goes. Never give unlabeled value dumps. " \
    "For tables, registers, memory rows, offsets, or blanks, use one labeled cell per line in top-to-bottom row order and left-to-right column order. " \
    "Preserve exact visible labels when readable, like mul3, eax, before, after. " \
    "Example: mul3 +3 before = 00. eax after = 00000024. " \
    "If a graph would help, describe it in words only. " \
    "Then give short work. " \
    "Plaintext only. No markdown, bullets, code blocks, LaTeX, or special formatting. " \
    "ASCII math only. End with END OF MESSAGE."

#define CAMERA_RECAP_SUFFIX \
    " Before answering, first restate the visible problem or prompt in plain words. " \
    "If the image contains symbols, operators, or notation, rewrite them as plain words or simple ASCII math. " \
    "Do this only for what you can actually read from the image."
