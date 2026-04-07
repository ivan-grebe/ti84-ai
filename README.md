# TI-84 AI

Custom firmware for a `TI-84 Plus` with an internal `Seeed Studio XIAO ESP32S3 Sense` and `OV5640` camera.

This project lets a stock TI-84 Plus trigger an internal ESP32 over the 2.5mm link port, download a TI-BASIC front-end, connect to WiFi, and send either typed prompts or camera captures to an LLM.

## Features

- Install `prgmTIAI` from the calculator with `Send({1})`
- Ask fresh text questions from the calculator
- Reply to the last text answer with calculator-side reply mode
- Take photos and send them to a vision-capable model
- Store WiFi credentials and API key in ESP32 flash
- Preview the live camera feed and the last uploaded JPEG over WiFi

## Hardware

- `TI-84 Plus` (non-CE)
- `Seeed Studio XIAO ESP32S3 Sense`
- `OV5640` camera
- Internal wiring from the ESP32 to the TI link port

This repo is for a custom internal mod, not a drop-in accessory.

## Repo Layout

- `platformio.ini` - PlatformIO environment and dependency list
- `include/config.h` - pins, prompts, model, camera settings, AP defaults
- `include/camera_pins.h` - OV5640 pin mapping for the XIAO Sense
- `include/program_data.h` - generated tokenized TI-BASIC payload
- `src/main.cpp` - calculator link handling and command dispatch
- `src/camera.h` - camera capture and preview helpers
- `src/wifi_manager.h` - WiFi, config portal, and camera debug page
- `src/openai_client.h` - OpenAI request building and response parsing
- `tools/build_program.py` - TI-BASIC source and tokenizer
- `tools/capture_serial.ps1` - serial capture helper
- `tools/start_capture_background.ps1` - background capture helper
- `tools/stop_capture_background.ps1` - stop background capture helper

## Build And Flash

### Requirements

- `PlatformIO`
- `Python 3`
- a connected `XIAO ESP32S3 Sense`

### Build

```powershell
platformio run
```

### Flash

```powershell
platformio run --target upload
```

### Serial Monitor

```powershell
platformio device monitor --port COM4 --baud 115200
```

## Quick Start

1. Flash the ESP32 firmware.
2. On the TI-84 home screen, run:

```text
Send({1})
```

3. Open `prgmTIAI`.
4. Use `3:SETTINGS -> 3:CONFIGURE` to enter WiFi credentials and an OpenAI API key.
5. Use `1:SEND MESSAGE` or `2:TAKE PHOTO`.

## Calculator Behavior

Main menu:

- `1:SEND MESSAGE`
- `2:TAKE PHOTO`
- `3:SETTINGS`
- `0:EXIT`

Navigation:

- `CLEAR` backs out of the current screen
- `CLEAR` on the top-level menu exits the program
- text responses scroll vertically
- `ALPHA` from a response opens reply mode

Conversation behavior:

- `SEND MESSAGE` starts fresh
- reply mode includes only the last text exchange as context
- photo solves do not include prior text-chat history

## Configuration

Important defaults live in `include/config.h`:

- TI link pins
- unlock password used by the TI-BASIC client
- camera frame size and JPEG quality
- OpenAI host, path, and model
- config AP and debug AP details
- system and camera prompts

If you change `tools/build_program.py`, regenerate the embedded calculator program:

```powershell
python tools/build_program.py
```

Then rebuild and reflash the firmware. After flashing, run `Send({1})` again on the calculator to install the updated `TIAI` program.

## WiFi And Camera Debug

The firmware supports:

- a config portal AP for entering WiFi credentials and an API key
- a camera debug page that shows a live preview and the last JPEG sent to the model

The current SSIDs, passwords, and debug IP defaults are defined in `include/config.h`.

## Security Notes

- Do not commit real serial logs. They may include WiFi SSIDs or other debugging details.
- Do not hardcode private API keys in source files.
- Consider changing the default AP passwords in `include/config.h` before publishing a build.
- `src/openai_client.h` currently uses `client.setInsecure()` for TLS on-device. That is convenient for prototyping, but it is not ideal for hardened production use.

## Troubleshooting

### `Send({1})` does not install `TIAI`

- Verify the link wiring.
- Verify `PIN_TIP` and `PIN_RING` in `include/config.h`.
- Check the serial log for calculator traffic.

### Text or photo requests hang

- Confirm WiFi is connected in `SETTINGS`.
- Use the serial capture tools in `tools/`.
- If image requests become unreliable, lower `CAM_FRAMESIZE` or raise `CAM_JPEG_QUALITY` slightly.

### Vision results are weak

- Use the camera debug page to inspect the actual capture.
- Improve framing, lighting, and distance.
- Tune `CAM_FRAMESIZE`, `CAM_JPEG_QUALITY`, and `CAMERA_PROMPT`.

## Known Constraints

- This is designed for the `TI-84 Plus`, not the `TI-84 Plus CE`.
- Large image payloads can still stress TLS, heap, and WiFi stability on the ESP32.
- The silent-link program transfer may still log `No ACK after EOT` even when install succeeds.

## Publishing Checklist

- remove any local logs
- confirm no real WiFi names are left in files
- confirm no API keys were hardcoded
- change the default AP passwords if you plan to distribute builds

## License

No license file is included yet. Add one before publishing if you want others to reuse the code under explicit terms.
