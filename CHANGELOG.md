# Changelog

## Beta 0.9 — Stage 5.10 baseline

Public-test package based on the fully tested Stage 5.10 firmware.

Features:

- single Dreamcast Maple port on GPIO21/GPIO22
- BlueRetro Bluetooth controller support
- 128 KiB persistent virtual VMU
- VMU WRITING/SAVED/READY status
- two-phase cold boot
- automatic SD VMU backup/previous-backup rotation
- internal VMU restore from SD when internal image is absent
- animated NiceMCU display
- live Bluetooth status
- SD backup OK/ERROR/UNKNOWN status
- Windows factory/update flashing helper
- GitHub-Pages-ready web flasher files

Known beta limitation:

- normal operation still requires USB-C power
- Dreamcast controller-port +5 V power is not yet released/tested
