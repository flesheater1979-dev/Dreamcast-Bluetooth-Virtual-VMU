# Troubleshooting

## Nothing appears on the LCD

Confirm you have the exact NiceMCU-32S-DEV 2.8" ESP32 board. This build assumes ST7789 pins 13/14/15/12/2/25.

Re-flash using Factory Install if the board has unknown firmware/partitioning.

## The serial monitor begins with garbage characters

Normal. The ROM/bootloader and app output use different baud stages. Runtime debug output for this build is normally monitored at 921600 baud.

## Bluetooth remains on PAIRING

- put the controller in genuine Bluetooth pairing mode
- remove/forget old pairing on the controller if necessary
- reboot the adapter and try again
- keep the controller close to the ESP32 during first pairing

## Dreamcast does not see the adapter

Check:

- Maple data → GPIO21 and GPIO22
- ground connection is solid
- the cable's signal mapping was verified rather than assumed from colours
- only one Dreamcast Maple port is configured in this beta

## VMU asks to format

On a fresh installation, this is expected. Format the virtual VMU once through Dreamcast.

## Save disappears after a Factory Install

Expected. Factory Install erases the ESP32 internal flash, including its internal VMU image.

If you have `/VMU/MC.BIN` on the SD card, a subsequent cold boot may restore it when the internal image is missing.

Use **Update Only** for normal firmware updates once this beta is installed.

## SD:BACKUP ERROR

The VMU can still operate internally.

Check:

- card is inserted
- FAT32 filesystem
- card contacts
- try another known-good card

The firmware does not format the SD card.

## Boot happens twice

Expected. Cold boot first performs the SD backup phase, then reboots into the timing-sensitive Dreamcast runtime with SD unmounted.

## Windows flasher cannot find esptool

Install Python 3, then run:

`py -m pip install esptool`

Re-run `FLASH-WINDOWS.bat`.

## Wrong COM port

Disconnect the NiceMCU and note which COM port disappears, then reconnect it and select that port.
