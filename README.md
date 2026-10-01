# Dreamcast Bluetooth + Virtual VMU for NiceMCU — Beta 0.9

A community beta firmware for the **NiceMCU-32S-DEV 2.8" ESP32 display board** that combines:

- BlueRetro Bluetooth controller support for Dreamcast
- a persistent 128 KiB virtual VMU
- automatic microSD VMU backup on cold boot
- a 2.8" status display with Bluetooth, VMU and SD-backup feedback
- single Dreamcast controller-port Maple connection

**Firmware build:** BlueRetro v25.04 derivative, internal milestone **Stage 5.10**.

## Beta status

The core functions have been tested successfully on real hardware:

- controller connects and works on Dreamcast
- Dreamcast detects and formats the virtual VMU
- games can save and reload
- saves survive reset and complete power loss
- `VMU:WRITING` and `VMU:SAVED` display feedback works
- SD boot backup reports `SD:BACKUP OK`
- animated display runs alongside Bluetooth + Maple + VMU

### Important power limitation for Beta 0.9

**Power the NiceMCU from USB-C. Leave the Dreamcast controller-port +5 V wire disconnected.**

Controller-port power is being developed separately and is not part of this public beta yet.

## Supported hardware

This build is for the tested board only:

- NiceMCU-32S-DEV 2.8" IPS
- original ESP32 (not ESP32-C3/C6/S3)
- 4 MB flash
- ST7789 240×320 display
- onboard microSD slot

The firmware assumes this display wiring:

| Function | ESP32 GPIO |
|---|---:|
| LCD MOSI | 13 |
| LCD SCLK | 14 |
| LCD CS | 15 |
| LCD DC | 12 |
| LCD RESET | 2 |
| LCD backlight | 25 |

The microSD path used by this build is:

| Function | ESP32 GPIO |
|---|---:|
| SD CS | 5 |
| SD CLK | 18 |
| SD MISO | 19 |
| SD MOSI | 23 |

## Dreamcast wiring

### Tested cable colour mapping

> **Cable colours are not guaranteed between different controller cables. Verify the connector signals before wiring another cable.**

For the cable used during development:

| Tested wire | Function | NiceMCU |
|---|---|---|
| RED | Maple data line | GPIO21 |
| WHITE | Maple data line | GPIO22 |
| GREEN | Ground | GND |
| COPPER / shield | Ground | GND |
| BLUE | +5 V from Dreamcast | **DISCONNECTED in Beta 0.9** |

Official Dreamcast controller-port signals are D0, +5 V, GND, Sense and D1. Do not rely on wire colours from an unknown cable.

## First-time installation

For most testers, start with **docs/QUICK-START.md** or open **START-HERE.html**.

A factory install erases the ESP32's internal flash, including any previous internal VMU image and Bluetooth pairing data. It does **not** format or erase the microSD card.

## VMU and microSD

Runtime saves are stored internally as a 128 KiB VMU image.

At cold boot, before Bluetooth/display startup, the firmware briefly mounts the microSD card and:

1. rotates the previous SD VMU image to `/VMU/BACKUP/PREV.BIN`
2. copies the current internal VMU to `/VMU/MC.BIN`
3. unmounts the card before normal Dreamcast runtime starts

If the internal VMU image is missing but `/VMU/MC.BIN` exists, the boot-backup phase can restore it.

A FAT32 microSD card is recommended. A 32 GB card has been tested.

The VMU still runs from internal flash if no SD card is present, but the UI will report an SD backup error.

## Flashing choices

- **Factory install:** for a new board or recovery. Erases internal flash first.
- **Update only:** for an existing Beta 0.9/Stage 5.x installation with the same partition layout. Updates only the app at `0x10000`, preserving internal VMU/pairing data.

See `windows-flasher/README.txt`.

## Web flasher

`web-flasher/` contains a ready-to-host ESP Web Tools site. Host that folder over HTTPS (for example GitHub Pages) and testers can flash from Chrome/Edge without Python or command-line tools.

## Source / attribution

This firmware is derived from **BlueRetro v25.04** by darthcloud/Jacques Gagnon.

Upstream:
https://github.com/darthcloud/BlueRetro

BlueRetro is licensed under Apache License 2.0. The upstream license is included as `LICENSE-BLUERETRO-APACHE-2.0.txt`.

Modified and new files used for this build are provided under `source-mods/`, with a modification summary in `source-mods/MODIFIED-FILES.md`.

Dreamcast is a Sega trademark. This community project is not affiliated with or endorsed by Sega or the BlueRetro project.

## Support the project

If you find this project useful and want to support further development:

[☕ Buy Me a Coffee](https://buymeacoffee.com/flesheater)
