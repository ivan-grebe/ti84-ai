# TI-84 AI

Custom firmware for a modded TI-84 Plus with an internal Seeed Studio XIAO ESP32S3 Sense and OV5640 camera.

The calculator talks to the ESP32 over the 2.5mm TI link port. The ESP32 handles WiFi, camera capture, and OpenAI requests. A small TI-BASIC program called `TIAI` runs on the calculator as the UI.

## Features

- Send typed prompts to a vision-capable model from the calculator
- Take a photo and have it solved automatically
- Store WiFi credentials and API key in ESP32 flash
- Camera debug page served over WiFi
- One-command program install via `Send({1})`

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
  wifi_manager.h      - WiFi, setup portal, and debug page
  openai_client.h     - OpenAI request handling
tools/
  build_program.py    - TI-BASIC source and tokenizer
  capture_serial.ps1  - serial log capture (timed, auto-detects COM port)
  start_capture_background.ps1 - start serial capture in a background process
  stop_capture_background.ps1  - stop the background capture process
platformio.ini        - PlatformIO environment
```

## Development Setup

You need four things installed before you can build:

1. **Python 3** -- https://www.python.org/downloads/
   - During install, check **Add Python to PATH**
2. **Visual Studio Code** -- https://code.visualstudio.com/
3. **PlatformIO IDE** -- install it as a VS Code extension
4. **Git** -- https://git-scm.com/download/win (if not already installed)

After installing PlatformIO, restart VS Code, then open this project folder.

> If you use the VS Code extension you do not need to install PlatformIO Core separately.

PlatformIO docs: [VS Code extension](https://docs.platformio.org/en/latest/integration/ide/vscode.html) | [Core installation](https://docs.platformio.org/en/latest/core/installation/)

## Build and Flash

Build the firmware:

```powershell
platformio run
```

Flash to the ESP32:

```powershell
platformio run --target upload
```

Open the serial monitor:

```powershell
platformio device monitor --port COM4 --baud 115200
```

## First-Time Calculator Setup

Once the ESP32 is flashed, set up the calculator:

1. On the TI-84 home screen, type `Send({1})` and press ENTER.

   To find `Send(`: press **[2ND]** > **[CATALOG]** > **[LN]** > scroll to `Send(`

   This installs `prgmTIAI` on the calculator.

2. Run `prgmTIAI`.
3. Go to **SETTINGS > CONFIGURE**. The calculator will display an AP name.
4. On your phone or computer, connect to that AP.
5. In the portal page that opens, enter your:
   - WiFi network name (SSID)
   - WiFi password
   - OpenAI API key
6. Back on the calculator, go to **SETTINGS > CONNECT**.

You're ready. Use **SEND MESSAGE** to type a prompt or **TAKE PHOTO** to snap and solve.

## Updating the Embedded TI-BASIC Program

If you edit the TI-BASIC source in `tools/build_program.py`:

```powershell
python tools/build_program.py          # regenerate the tokenized program
platformio run --target upload          # flash the updated firmware
```

Then on the calculator, run `Send({1})` again to install the new version of `TIAI`.

## Notes

- Camera quality and prompt behavior are configured in `include/config.h`
- Serial logs are not committed because they may contain local network details

## Known Limits

- Built for the TI-84 Plus, not the TI-84 Plus CE
- Large image uploads can stress the ESP32 network stack
- The silent install path may log `No ACK after EOT` even on a successful install
