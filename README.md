# TI-84 AI

Custom firmware for a modded `TI-84 Plus` with an internal `Seeed Studio XIAO ESP32S3 Sense` and `OV5640` camera.

The calculator talks to the ESP32 over the normal 2.5mm TI link port. The ESP32 handles WiFi, camera capture, and OpenAI requests. The calculator downloads a small TI-BASIC front-end called `TIAI` and uses that as the on-device UI.

## What It Does

- Install `prgmTIAI` with `Send({1})`
- Send typed prompts from the calculator
- Take a photo and send it to a vision-capable model
- Store WiFi credentials and API key in ESP32 flash
- Serve a simple camera debug page over WiFi

## Hardware

- `TI-84 Plus` (non-CE)
- `Seeed Studio XIAO ESP32S3 Sense`
- `OV5640` camera
- Internal wiring to the calculator link port

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

## Build

```powershell
platformio run
```

## Flash

```powershell
platformio run --target upload
```

## First-Time Setup

1. Flash the ESP32 firmware.
2. On the calculator home screen, run:

```text
Send({1})
```
(You can access this easily by pressing [2ND] + [CATALOG], then pressing [LN] and scrolling down and selecting Send.)

3. Run `prgmTIAI`.
4. Open `SETTINGS -> CONFIGURE`.
5. Connect to the setup AP shown on the calculator and enter:
   - WiFi SSID
   - WiFi password
   - OpenAI API key

After that, use `SETTINGS -> CONNECT`, then `SEND MESSAGE` or `TAKE PHOTO`.

## Notes

- If you change `tools/build_program.py`, regenerate `include/program_data.h` and reflash.
- After changing the embedded TI-BASIC program, run `Send({1})` again on the calculator.
- Camera quality and prompt behavior are controlled in `include/config.h`.

## Known Limits

- Built around the `TI-84 Plus`, not the `TI-84 Plus CE`
- Large image uploads can still stress the ESP32 network stack
- The silent install path may still log `No ACK after EOT` even when install succeeds
