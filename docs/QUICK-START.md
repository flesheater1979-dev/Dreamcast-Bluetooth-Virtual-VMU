# Quick Start — Beta 0.9

## 1. Before connecting anything

You need:

- NiceMCU-32S-DEV 2.8" ESP32 board
- Dreamcast controller cable/plug
- USB-C cable for NiceMCU power
- optional FAT32 microSD card (recommended)
- supported Bluetooth controller

**Do not connect the Dreamcast +5 V wire to the NiceMCU in Beta 0.9.**

## 2. Wire the Dreamcast Maple connection

For the cable used during development:

- RED → GPIO21
- WHITE → GPIO22
- GREEN → GND
- COPPER/shield → GND
- BLUE → leave completely disconnected and insulated

**Warning:** cable colours can vary. Verify your own cable before copying the colour mapping.

## 3. Insert the microSD card

Use FAT32. The firmware creates the VMU folders/files automatically. It never formats the card.

## 4. Flash the firmware

### Windows fallback method

Open `windows-flasher` and double-click:

`FLASH-WINDOWS.bat`

The helper will:

1. detect/list COM ports
2. verify the included firmware hashes
3. offer Factory Install or Update Only
4. run Espressif esptool

Python 3 is required for this fallback flasher. If `esptool` is missing, the helper offers to install it with pip.

### Browser method

If the project owner has hosted the included `web-flasher` folder on GitHub Pages/HTTPS:

1. use Chrome or Edge
2. connect NiceMCU by USB
3. click Install
4. choose the ESP32 serial port

## 5. First boot

A normal cold boot has two phases:

1. SD backup/restore phase
2. automatic reboot into Dreamcast runtime

The second phase starts Bluetooth, Maple, VMU and the animated display.

## 6. Pair a controller

Put your controller into Bluetooth pairing mode.

The display should change from:

`BT:PAIRING`

to:

`BT:CONNECTED`

## 7. Set up the virtual VMU

If Dreamcast asks to format the VMU, format it once.

Create a game save. During saving you should see:

`VMU:WRITING`

then:

`VMU:SAVED`

then:

`VMU:READY`

## 8. Test persistence

After creating a save:

1. load it once
2. turn off the Dreamcast
3. unplug NiceMCU USB power
4. reconnect and boot again
5. confirm the save is still present

## 9. SD backup check

After a successful cold boot, the display should report:

`SD:BACKUP OK`

The card should contain:

- `/VMU/MC.BIN`
- `/VMU/BACKUP/PREV.BIN` after a previous backup exists

Both VMU images are expected to be 131072 bytes.
