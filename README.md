# PokeAlarm 🔥

A Pokémon-themed alarm clock firmware for small ESP32 gadgets, with an animated GIF background, weather, a web interface, and spoken messages.

The repository holds two firmware projects that share the same features and web interface, each adapted to its own hardware:

| Folder | Device | Display |
|---|---|---|
| [`PokeSveglia/`](PokeSveglia) | **XiaoZhi "DeepSeek AI" voice robot** (Spotpear ESP32-S3-1.28-BOX, touch + battery version) | 1.28" round, 240×240, touch |
| [`m5stack/`](m5stack) | **M5Stack Core2 for AWS IoT EduKit** (K010-AWS) | 2.0" rectangular, 320×240, touch |

The on-device interface is in Italian ("sveglia" means alarm clock). The code comments are in Italian too.

## Features

**Clock and alarms**
- Animated GIF background (a Charizard sprite is included).
- Time from NTP. The time zone and daylight saving are detected automatically from your location.
- Up to 5 alarms, each with its own weekdays, label, and on/off switch. An alarm with no days set rings once.
- Classic "beep-beep-beep-beep" sound with a volume that rises gradually.
- While an alarm rings:
  - tap the screen to snooze;
  - long-press to stop;
  - on the M5Stack, you can also shake the device to stop it.

**Screens.** Swipe right for the weather and left for the alarm list:
- **Clock**: GIF background, time, date, next alarm, and battery level. The battery icon shows a bolt when USB is connected.
- **Weather**: today and tomorrow (icon, max/min temperature, chance of rain), from [Open-Meteo](https://open-meteo.com). No API key is needed, and the location is detected from your public IP.
- **Alarms**: tap a row to turn that alarm on or off.

**Web interface.** Open it at the device IP or at `http://pokesveglia.local` / `http://pokesveglia-m5.local`:
- edit alarms and test the sound;
- **send messages**: they are shown on screen and read aloud, after a short chime;
- upload GIFs and pick which one to use. The browser converts any GIF to the screen size, with no external libraries, so it also works without internet;
- move, resize, and change the speed of the GIF, saved separately for each GIF;
- volume, day/night brightness, and city override;
- Wi-Fi setup.

**Setup and connectivity**
- **Captive-portal setup**: on first boot the device creates its own Wi-Fi network (`PokeSveglia-Setup` or `PokeSveglia-M5`, password `pokemon123`).
- Robust Wi-Fi: several attempts at boot and automatic reconnection. If the saved network stays unreachable, the setup network turns itself back on.
- **Offline time**: when no internet is available, opening the web page from your phone copies the phone's time to the device.

### M5Stack-only extras
- **Hardware RTC**: alarms keep working after a power-off, even without Wi-Fi.
- **LED bar**: while the alarm rings, a light runs around the bar and changes color on every lap (yellow → red → green). The vibration motor joins after 10 s.
- **Night light**: a warm amber glow from 22:00 until the first alarm of the night. The start time and intensity can be changed.
- **Knock twice** on the case to turn the night light on or off, detected by the accelerometer.
- Physical touch buttons A/B/C below the screen: weather, clock, and alarms.

## Getting started

Both projects use [PlatformIO](https://platformio.org) (VS Code extension).

1. Open **one** of the project folders (`PokeSveglia/` or `m5stack/`) in VS Code; it is the folder that contains `platformio.ini`.
2. In the PlatformIO toolbar, run **Upload Filesystem Image** to copy the GIFs in `data/gifs` to the device.
3. Then run **Upload** to flash the firmware.
4. On first boot, connect your phone to the setup Wi-Fi network, open `http://192.168.4.1`, and choose your home network.

Each folder has its own README, in Italian, with device-specific details and troubleshooting.

> **Tip for the XiaoZhi robot:** back up the original firmware before flashing, so you can restore it later.
> ```bash
> ~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 read_flash 0 0x1000000 backup_xiaozhi.bin
> ```

### Adding GIFs from a computer
```bash
python3 tools/convert_gif.py my.gif data/gifs/pikachu.gif --pixel
```
This needs Pillow (`pip3 install pillow`). The script produces 240×240 frames for the robot and 320×240 frames for the M5Stack.

## Hardware notes

**XiaoZhi robot** (ESP32-S3 N16R8)
- GC9A01 display on SPI.
- CST816D touch controller.
- ES8311 audio codec with amplifier.
- Battery sensed on ADC, with a power-hold pin.

The pin map comes from the open-source [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) board definition `sp-esp32-s3-1.28-box`. The display orientation, color order, and touch direction can be tuned in `PokeSveglia/include/board.h`.

**M5Stack Core2 for AWS** (ESP32, 16 MB flash, 8 MB PSRAM). Display, touch, speaker, RTC, IMU, and power are handled by [M5Unified](https://github.com/m5stack/M5Unified). The SK6812 LED bar is on GPIO 25.

## Libraries
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX) and [M5Unified / M5GFX](https://github.com/m5stack/M5Unified): display and hardware.
- [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF): GIF decoding.
- [ArduinoJson](https://arduinojson.org): configuration and APIs.
- [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio): MP3 playback for spoken messages.
- [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel): LED bar (M5Stack).

## Disclaimer
- This is a personal hobby project and is not affiliated with Nintendo, Game Freak, or The Pokémon Company. Pokémon and Charizard are trademarks of their respective owners. The included sprite is used only as an example background.
- Spoken messages use Google Translate's unofficial text-to-speech endpoint. It is free and needs no API key, but it may change or stop working at any time.
- Location is looked up through [ip-api.com](https://ip-api.com) and weather through [Open-Meteo](https://open-meteo.com), both free services without registration.
