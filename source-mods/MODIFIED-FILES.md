# Modified / Added Files

Upstream base: `darthcloud/BlueRetro`, tag `v25.04`.

The beta firmware was built from modified BlueRetro sources. Copies relevant to the NiceMCU Dreamcast build are included here.

## Modified upstream files

- `main/main.c` — two-phase boot; SD result handoff; NiceMCU display/storage startup; larger init-task stack
- `main/CMakeLists.txt` — adds NiceMCU Stage 5 display/storage sources
- `main/adapter/memory_card.c` — persistent virtual VMU handling and RAM-only UI write/store telemetry
- `main/system/manager.c` — single Dreamcast-port configuration and GPIO protection for NiceMCU LCD pins
- `main/wired/maple.c` — single NiceMCU Dreamcast Maple port
- `build-stage5.sh` — reproducible Stage 5.10 build checks/version
- build configuration for HW1 Dreamcast

## New NiceMCU files

- `main/nicemcu_stage5_display.c`
- `main/nicemcu_stage5_display.h`
- `main/nicemcu_stage5_storage.c`
- `main/nicemcu_stage5_storage.h`
- `main/nicemcu_stage5_logo.h`

The copies of modified original source files in this release have a distribution notice comment added at the top. That comment does not change program behaviour.

## Rebuilding

1. Clone BlueRetro.
2. Check out tag `v25.04`.
3. Copy the files in this folder over the matching paths.
4. Use the upstream-compatible IDF environment/Docker image used by the project.
5. Run `build-stage5.sh`.

Development build container used for this beta:

`ghcr.io/darthcloud/idf-blueretro:v5.5.0_2024-12-02`

The tested firmware reports:

`v25.04 hw1 dc single stage5.10`
