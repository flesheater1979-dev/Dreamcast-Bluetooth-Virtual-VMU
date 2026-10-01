Dreamcast Bluetooth + Virtual VMU Beta 0.9 - Windows Flasher

EASIEST WINDOWS FALLBACK
------------------------
1. Extract the whole release ZIP. Do not run this from inside the ZIP preview.
2. Connect NiceMCU by USB-C.
3. Make sure the Dreamcast +5 V wire is DISCONNECTED.
4. Double-click FLASH-WINDOWS.bat.
5. Choose Factory Install for a new/recovery board.
6. Choose Update Only only for an existing compatible installation.

Requirements:
- Windows PowerShell
- Python 3

If Python has no esptool package, the helper offers to install it using pip.

FACTORY INSTALL
---------------
Erases ESP32 internal flash, then writes:
0x1000  bootloader.bin
0x8000  partition-table.bin
0xD000  ota_data_initial.bin
0x10000 Dreamcast-BT-VMU-Beta-v0.9-app.bin

Factory Install erases internal VMU data and Bluetooth pairing data.
It does NOT erase the microSD card.

UPDATE ONLY
-----------
Writes only:
0x10000 Dreamcast-BT-VMU-Beta-v0.9-app.bin

This is intended to preserve existing internal VMU and NVS/pairings.

The helper verifies SHA-256 hashes before flashing.
