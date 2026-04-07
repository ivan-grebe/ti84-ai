# TI-84 AI

Custom firmware for a modded `TI-84 Plus` with an internal `Seeed Studio XIAO ESP32S3 Sense` and `OV5640` camera.

The calculator talks to the ESP32 over the normal 2.5mm TI link port. The ESP32 handles WiFi, camera capture, and OpenAI requests. The calculator downloads a small TI-BASIC front-end called `TIAI` and uses that as the on-device UI.

## What It Does

- install `prgmTIAI` with `Send({1})`
- send typed prompts from the calculator
- take a photo and send it to a vision-capable model
- store WiFi credentials and API key in ESP32 flash
- serve a simple camera debug page over WiFi

## Hardware

- `TI-84 Plus` (non-CE)
- `Seeed Studio XIAO ESP32S3 Sense`
- `OV5640` camera
- internal wiring to the calculator link port

This repo is for a custom internal hardware mod, not a general-purpose accessory.

## Wiring

Current link-pin config:

- TI link `tip` -> `D0`
- TI link `ring` -> `D1`

See `include/config.h` and `include/camera_pins.h` for the exact firmware pin definitions.

## Repo Layout

- `platformio.ini` - PlatformIO environment
- `include/config.h` - link pins, prompts, model, camera settings
- `include/camera_pins.h` - OV5640 pin mapping
- `include/program_data.h` - generated tokenized TI-BASIC program
- `src/main.cpp` - TI link handling and command dispatch
- `src/camera.h` - camera capture and preview helpers
- `src/wifi_manager.h` - WiFi, setup portal, and debug page
- `src/openai_client.h` - OpenAI request handling
- `tools/build_program.py` - TI-BASIC source and tokenizer

## Development Setup

The easiest way to build this project is with `Visual Studio Code` and the `PlatformIO IDE` extension.

Install these first:

- `Python 3`
- `Visual Studio Code`
- `PlatformIO IDE`
- `Git`

Recommended Windows setup:

1. Install Python 3 from `https://www.python.org/downloads/`
2. During Python setup, enable `Add Python to PATH`
3. Install VS Code from `https://code.visualstudio.com/`
4. Open VS Code and install the `PlatformIO IDE` extension
5. Install Git from `https://git-scm.com/download/win` if it is not already installed
6. Reopen VS Code after PlatformIO finishes installing
7. Open this project folder in VS Code

Official PlatformIO docs:

- VS Code extension: `https://docs.platformio.org/en/latest/integration/ide/vscode.html`
- Core installation: `https://docs.platformio.org/en/latest/core/installation/`

If you use the VS Code extension, you usually do not need to install PlatformIO Core separately.

## Build

```powershell
platformio run
```

## Flash

```powershell
platformio run --target upload
```

## Serial Monitor

```powershell
platformio device monitor --port COM4 --baud 115200
```

## First-Time Setup

1. Flash the ESP32 firmware:

```powershell
platformio run --target upload
```

2. On the TI-84 home screen, run:

```text
Send({1})
```

One easy way to enter it is:

- press `[2ND]`
- press `[CATALOG]`
- press `[LN]`
- scroll to `Send(`

3. Run `prgmTIAI`
4. Open `SETTINGS -> CONFIGURE`
5. Connect to the setup AP shown on the calculator
6. Enter:
   - WiFi SSID
   - WiFi password
   - OpenAI API key

After that, use `SETTINGS -> CONNECT`, then `SEND MESSAGE` or `TAKE PHOTO`.

## Updating The Embedded TI-BASIC Program

If you change `tools/build_program.py`, regenerate the embedded calculator program:

```powershell
python tools/build_program.py
```

Then reflash the ESP32:

```powershell
platformio run --target upload
```

After flashing, go back to the calculator and run:

```text
Send({1})
```

That installs the new `TIAI` program on the calculator.

## Notes

- camera quality and prompt behavior are controlled in `include/config.h`
- real serial logs are intentionally not committed because they may contain local network details

## Known Limits

- built around the `TI-84 Plus`, not the `TI-84 Plus CE`
- large image uploads can still stress the ESP32 network stack
- the silent install path may still log `No ACK after EOT` even when install succeeds
