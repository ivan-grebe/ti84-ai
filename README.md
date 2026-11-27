# TI-84 AI

Custom firmware for a modded TI-84 Plus with an internal Seeed Studio XIAO ESP32S3 Sense and OV5640 camera.

## Features

- Send typed prompts to a vision-capable OpenAI model from the calculator
- Reply to continue a conversation with context from previous messages
- Take a photo and have it solved automatically
- Web config portal for WiFi, API key, camera quality, and debug AP settings
- Three camera quality profiles: Fast (VGA), Balanced (SXGA), Sharp (UXGA)
- Settings stored in ESP32 flash (NVS) and persist across reboots
- One-command program install via `Send({1})` with automatic retry
- Auto-reconnect and DNS recovery for flaky WiFi

## Hardware

| Part | Details |
|------|---------|
| Calculator | TI-84 Plus (non-CE) |
| Microcontroller | Seeed Studio XIAO ESP32S3 Sense |
| Camera | Comes with the S3 Sense (OV2640, OV3660, or OV5640) |
| Connection | Internal wiring to calculator link port |

## Wiring

Follow the soldering in this video: [How To Install ChatGPT On Your Calculator (Full Tutorial)](https://www.youtube.com/watch?v=7V_rGwuT2pE). Its wiring diagram is [here](https://imgur.com/a/EiM788S).

Three things are different from the video:

1. **Use a XIAO ESP32S3 Sense, not the ESP32C3 he uses.** This firmware does not run on the C3. The S3 has the same pads in the same places, so solder it exactly the same way. Make sure you get the **Sense** version: the plain XIAO ESP32S3 has nowhere to plug in a camera.
2. **Skip the firmware section (3:34 to 4:51).** Flash this repo instead (see [Build and Flash](#build-and-flash)).
3. **Use NiMH rechargeable AAA batteries (1.2V each).** The ESP32 runs straight off the calculator's batteries. Four fresh alkaline AAAs can put out over 6V, which is more than the S3's 5V pin is meant to take. Four NiMH cells give about 4.8V.

| Wire in the video | ESP32 pin | Used by this firmware as |
|-------------------|-----------|--------------------------|
| Orange, to the link jack | D0 | Tip |
| Blue, to the pad in the middle of the board | D1 | Not used (solder it anyway, it's harmless) |
| Green, to the link jack | D2 | Ring |
| Red, to battery + | VUSB (5V) | Power |
| Black, to ground | GND | Ground |

### Antenna

The WiFi antenna is loose in the box. You must install it. Without the antenna, the WiFi signal is very weak.

1. Find the small round connector on the front of the XIAO. It is at the bottom left.
2. Put one side of the antenna connector into the connector on the XIAO.
3. Push down on the other side until it clicks.
4. Attach the antenna flat to the inside of the back cover.

### Camera placement

The video doesn't use a camera, so this part is up to you. The camera is already part of the S3 Sense (it plugs into the board's own connector) so it doesn't need extra wiring. You just need to place it so it can see out of the case.

The camera is optional. If you skip it, everything else still works, and **TAKE PHOTO** just shows `ERR:CAMERA FAIL`.

Depending on when it was made, the S3 Sense comes with an OV2640, OV3660, or OV5640 camera. All three should work, but the image settings were tuned on an OV5640, so photos from the others may look a little different.

## Development Setup

You need three things installed before you can build (or just point your AI agent of choice at this repo):

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
5. Open a browser and go to **http://192.168.4.1**. The page does not open automatically. If your phone shows "no internet", stay connected to the network. Enter these items:
   - WiFi network name (SSID) and password
   - OpenAI API key
6. Save. The ESP32 will reboot with the new settings.
7. Back on the calculator, go to **SETTINGS > CONNECT**.

You're ready. Use **SEND MESSAGE** to type a prompt, **REPLY** to follow up, or **TAKE PHOTO** to snap and solve. Scroll through long responses with the up/down keys.

When finished, pop out a battery to completely kill power to the ESP32. Use NiMH rechargeables (1.2V).

## Camera Debug Hotspot

When enabled and connected to WiFi, the ESP32 also starts a secondary AP called **TI84CAM** (password: `12345678`). Connect to it and visit `http://192.168.8.1:81/` to see a live camera preview. The debug AP can be toggled on or off from the config portal.

## Updating the Embedded TI-BASIC Program

If you edit the TI-BASIC source in `tools/build_program.py`:

```bash
python3 tools/build_program.py         # regenerate the tokenized program
platformio run --target upload         # flash the updated firmware
```

Then on the calculator, run `Send({1})` again to install the new version of `TIAI`.
