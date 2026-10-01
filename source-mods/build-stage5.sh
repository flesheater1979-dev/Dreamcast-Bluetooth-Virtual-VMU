set -euxo pipefail
source /opt/esp/idf/export.sh
cd /work
rm -rf build
cp configs/hw1/dreamcast sdkconfig
printf "v25.04 hw1 dc single stage5.10
" > version.txt
export BR_HW=_hw1
export BR_SYS=_dreamcast

grep -Eq 'static[[:space:]]+uint32_t[[:space:]]+port_cnt[[:space:]]*=[[:space:]]*1[[:space:]]*;' main/wired/maple.c
grep -q '#define LED_P1_PIN 4' main/system/manager.c
grep -q 'NiceMCU LCD GPIO protection' main/system/manager.c
grep -q 'nicemcu_stage5_display.c' main/CMakeLists.txt
grep -q 'nicemcu_stage5_storage.c' main/CMakeLists.txt
grep -q 'Stage 5.10 phase 1' main/main.c
! grep -q 'nicemcu_vmu_capture_lcd' main/wired/maple.c
! grep -q 'ID_VMU_LCD' main/wired/maple.c

grep -q 'nicemcu_vmu_write_seq' main/adapter/memory_card.c
grep -q 'VMU:WRITING' main/nicemcu_stage5_display.c
grep -q 'SD:BACKUP OK' main/nicemcu_stage5_display.c
idf.py reconfigure
idf.py build
