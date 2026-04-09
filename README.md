# TI-84 AI

Custom firmware for a modded TI-84 Plus with an internal Seeed Studio XIAO ESP32S3 Sense and OV5640 camera.

The calculator talks to the ESP32 over the 2.5mm TI link port. The ESP32 handles WiFi, camera capture, and OpenAI requests. A small TI-BASIC program called `TIAI` runs on the calculator as the UI.

## Features

- Send typed prompts to a vision-capable OpenAI model from the calculator
- Reply to continue a conversation with context from previous messages
- Take a photo and have it solved automatically
- Scroll through long responses line by line
- Web config portal for WiFi, API key, camera quality, and debug AP settings
- Three camera quality profiles: Fast (VGA), Balanced (SXGA), Sharp (UXGA)
- Camera debug hotspot (TI84CAM) with live preview page on port 81
- Toggle the debug AP on or off from the config portal
- Settings stored in ESP32 flash (NVS) and persist across reboots
- One-command program install via `Send({1})` with automatic retry
- Auto-reconnect and DNS recovery for flaky WiFi

## Hardware

| Part | Details |
|------|---------|
| Calculator | TI-84 Plus (non-CE) |
| Microcontroller | Seeed Studio XIAO ESP32S3 Sense |
| Camera | OV5640 |
| Connection | Internal wiring to calculator link port |

This is a custom internal hardware mod, not a general-purpose accessory.

## Wiring

| TI Link Pin | ESP32 Pin |
|-------------|-----------|
| Tip | D0 |
| Ring | D1 |

See `include/config.h` and `include/camera_pins.h` for the full pin definitions.

## Repo Layout

```
include/
  config.h            - link pins, prompts, model, camera settings
  camera_pins.h       - OV5640 pin mapping
  program_data.h      - generated tokenized TI-BASIC program
src/
  main.cpp            - TI link handling and command dispatch
  camera.h            - camera capture and preview helpers
  wifi_manager.h      - WiFi, config portal, debug AP, and camera debug server
  openai_client.h     - OpenAI Responses API client with retry and DNS recovery
  web_ui.h            - config portal HTML rendering
tools/
  build_program.py    - TI-BASIC source and tokenizer
  capture_serial.ps1  - serial log capture (timed, auto-detects COM port)
  start_capture_background.ps1 - start serial capture in a background process
  stop_capture_background.ps1  - stop the background capture process
platformio.ini        - PlatformIO environment
```

## Development Setup

You need three things installed before you can build:

1. **Python 3** -- https://www.python.org/downloads/
2. **Git** -- https://git-scm.com/downloads
3. **PlatformIO**
   - Option A: install the **PlatformIO IDE** VS Code extension
   - Option B: install **PlatformIO Core** for command-line builds (`pipx install platformio`)

After installing PlatformIO, open this project folder in VS Code or in your shell.

> If you use the VS Code extension you do not need to install PlatformIO Core separately.

PlatformIO docs: [VS Code extension](https://docs.platformio.org/en/latest/integration/ide/vscode.html) | [Core installation](https://docs.platformio.org/en/latest/core/installation/)

## Build and Flash

Build the firmware:

```bash
platformio run
```

Flash to the ESP32:

```bash
platformio run --target upload
```

List connected serial devices if PlatformIO does not auto-detect the board:

```bash
platformio device list
```

Flash to a specific port if needed:

```bash
platformio run --target upload --upload-port /dev/ttyACM0
```

Open the serial monitor:

```bash
platformio device monitor --baud 115200
```

Or target a specific port:

```bash
platformio device monitor --port /dev/ttyACM0 --baud 115200
```

On Linux, if upload or monitor fails with `Permission denied`, add your user to the `dialout` group and sign out/in before retrying:

```bash
sudo usermod -aG dialout "$USER"
```

## First-Time Calculator Setup

Once the ESP32 is flashed, set up the calculator:

1. On the TI-84 home screen, type `Send({1})` and press ENTER.

   To find `Send(`: press **[2ND]** > **[CATALOG]** > **[LN]** > scroll to `Send(`

   This installs `prgmTIAI` on the calculator.

2. Run `prgmTIAI`.
3. Go to **SETTINGS > CONFIGURE**. The calculator will display an AP name.
4. On your phone or computer, connect to the **TI84AI** network (password: `12345678`).
5. Open the config page that appears and enter:
   - WiFi network name (SSID) and password
   - OpenAI API key
   - Camera quality profile (optional, defaults to Balanced)
   - Whether to enable the TI84CAM debug hotspot (optional, on by default)
6. Save. The ESP32 will reboot with the new settings.
7. Back on the calculator, go to **SETTINGS > CONNECT**.

You're ready. Use **SEND MESSAGE** to type a prompt, **REPLY** to follow up, or **TAKE PHOTO** to snap and solve. Scroll through long responses with the up/down keys.

When finished, pop out a battery to completely kill power to the ESP32. A 1.5V or 1.2V battery works.

## Camera Debug Hotspot

When enabled and connected to WiFi, the ESP32 also starts a secondary AP called **TI84CAM** (password: `12345678`). Connect to it and visit `http://192.168.8.1:81/` to see a live camera preview. The debug AP can be toggled on or off from the config portal.

## Updating the Embedded TI-BASIC Program

If you edit the TI-BASIC source in `tools/build_program.py`:

```bash
python3 tools/build_program.py         # regenerate the tokenized program
platformio run --target upload         # flash the updated firmware
```

Then on the calculator, run `Send({1})` again to install the new version of `TIAI`.

## Notes

- Camera quality, model, and prompt behavior are configured in `include/config.h`
- The OpenAI client uses the Responses API (`/v1/responses`) with conversation state via `previous_response_id`
- Vision requests are built manually (not via ArduinoJson) to avoid duplicating large base64 strings in RAM
- Serial logs are not committed because they may contain local network details

## Known Limits

- Built for the TI-84 Plus, not the TI-84 Plus CE
- Large image uploads can stress the ESP32 network stack
- The silent install path may log `No ACK after EOT` even on a successful install
- Vision requests do not carry conversation history (each photo is standalone)
