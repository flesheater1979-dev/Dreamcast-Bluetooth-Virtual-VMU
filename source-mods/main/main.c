/* MODIFIED for Dreamcast Bluetooth + Virtual VMU NiceMCU Beta 0.9 (firmware Stage 5.10). See MODIFIED-FILES.md. */
/*
 * Copyright (c) 2019-2024, Jacques Gagnon
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp32/rom/ets_sys.h>
#include <soc/efuse_reg.h>
#include <esp_efuse.h>
#include "system/bare_metal_app_cpu.h"
#include "system/core0_stall.h"
#include "system/delay.h"
#include "system/fs.h"
#include "system/led.h"
#include "adapter/adapter.h"
#include "adapter/adapter_debug.h"
#include "adapter/config.h"
#include "bluetooth/host.h"
#include "wired/detect.h"
#include "wired/wired_bare.h"
#include "wired/wired_rtos.h"
#include "adapter/memory_card.h"
#include "system/manager.h"
#include "nicemcu_stage5_display.h"
#include "nicemcu_stage5_storage.h"
#include "tests/ws_srv.h"
#include "tests/coverage.h"
#include "sdkconfig.h"

static uint32_t chip_package = EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ6;

/* Stage 5.10 two-boot state.
 *
 * 1 (PENDING) is committed BEFORE SD is touched.  After the SD backup returns
 * we try to replace it with OK or ERROR.  If that second NVS update ever fails
 * or the board resets at exactly that point, PENDING still causes the next boot
 * to enter runtime mode rather than repeating the SD phase.  The only loss is
 * that the UI reports SD:BACKUP UNKNOWN for that session.
 */
#define S510_NVS_NS "s5boot"
#define S510_NVS_KEY "runtime"
#define S510_MARK_PENDING 1
#define S510_MARK_SD_OK 2
#define S510_MARK_SD_ERROR 3

static bool stage510_take_runtime_marker(uint8_t *marker_out) {
    nvs_handle_t h;
    uint8_t v = 0;
    if (marker_out) *marker_out = 0;
    if (nvs_open(S510_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_get_u8(h, S510_NVS_KEY, &v);
    if (e == ESP_OK && v >= S510_MARK_PENDING && v <= S510_MARK_SD_ERROR) {
        if (marker_out) *marker_out = v;
        nvs_erase_key(h, S510_NVS_KEY);
        nvs_commit(h);
        nvs_close(h);
        return true;
    }
    nvs_close(h);
    return false;
}

static bool stage510_set_runtime_marker(uint8_t value) {
    nvs_handle_t h;
    if (nvs_open(S510_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_u8(h, S510_NVS_KEY, value);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}

static void wired_init_task(void) {
#ifdef CONFIG_BLUERETRO_SYSTEM_UNIVERSAL
    detect_init();
    while (wired_adapter.system_id <= WIRED_AUTO) {
        if (config.magic == CONFIG_MAGIC && config.global_cfg.system_cfg < WIRED_MAX
            && config.global_cfg.system_cfg != WIRED_AUTO) {
            break;
        }
        delay_us(1000);
    }
    detect_deinit();

    if (wired_adapter.system_id >= 0) {
        ets_printf("# Detected system : %d: %s\n", wired_adapter.system_id, wired_get_sys_name());
    }
#else
    wired_adapter.system_id = HARDCODED_SYS;
    ets_printf("# Hardcoded system : %d: %s\n", wired_adapter.system_id, wired_get_sys_name());
#endif

    while (config.magic != CONFIG_MAGIC) {
        delay_us(1000);
    }

#ifdef CONFIG_BLUERETRO_SYSTEM_DC
    if (wired_adapter.system_id == DC) {
        config.out_cfg[0].acc_mode |= ACC_MEM;
    }
#endif

    if (config.global_cfg.system_cfg < WIRED_MAX && config.global_cfg.system_cfg != WIRED_AUTO) {
        wired_adapter.system_id = config.global_cfg.system_cfg;
        ets_printf("# Config override system : %d: %s\n", wired_adapter.system_id, wired_get_sys_name());
    }

    for (uint32_t i = 0; i < WIRED_MAX_DEV; i++) {
        adapter_init_buffer(i);
    }

    struct raw_fb fb_data = {0};
    const char *sysname = wired_get_sys_name();
    fb_data.header.wired_id = 0;
    fb_data.header.type = FB_TYPE_SYS_ID;
    fb_data.header.data_len = strlen(sysname);
    memcpy(fb_data.data, sysname, fb_data.header.data_len);
    adapter_q_fb(&fb_data);

    if (wired_adapter.system_id < WIRED_MAX) {
        wired_bare_init(chip_package);
    }
}

static void wl_init_task(void *arg) {
    uint32_t err = 0;

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    esp_ota_get_state_partition(running, &ota_state);

    chip_package = esp_efuse_get_pkg_ver();

#ifdef CONFIG_BLUERETRO_COVERAGE
    cov_init();
#endif

    err_led_init(chip_package);
    core0_stall_init();

#ifndef CONFIG_BLUERETRO_QEMU
    if (fs_init()) {
        err_led_set();
        err = 1;
        printf("FS init fail!\n");
    }
#endif

    int32_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    config_init(DEFAULT_CFG);

    uint8_t runtime_marker = 0;
    bool runtime_boot = stage510_take_runtime_marker(&runtime_marker);

    if (!runtime_boot) {
        printf("# Stage 5.10 phase 1: arm runtime marker BEFORE SD backup\n");
        if (!stage510_set_runtime_marker(S510_MARK_PENDING)) {
            printf("# Stage 5.10 ERROR: NVS runtime marker failed; starting runtime without SD backup\n");
        }
        else {
            bool sd_ok = nicemcu_stage5_storage_init();
            uint8_t result = sd_ok ? S510_MARK_SD_OK : S510_MARK_SD_ERROR;
            if (!stage510_set_runtime_marker(result)) {
                printf("# Stage 5.10 WARNING: SD result marker update failed; runtime will report UNKNOWN\n");
            }
            printf("# Stage 5.10 phase 1 complete: SD backup %s; restarting cleanly\n",
                   sd_ok ? "OK" : "FAILED/INTERNAL");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
            while (1) vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    int sd_status = NICEMCU_SD_STATUS_UNKNOWN;
    if (runtime_marker == S510_MARK_SD_OK) sd_status = NICEMCU_SD_STATUS_OK;
    else if (runtime_marker == S510_MARK_SD_ERROR) sd_status = NICEMCU_SD_STATUS_ERROR;

    printf("# Stage 5.10 phase 2: runtime boot; SD backup status=%s; SD untouched; starting BlueRetro + Dreamcast UI\n",
           sd_status == NICEMCU_SD_STATUS_OK ? "OK" :
           (sd_status == NICEMCU_SD_STATUS_ERROR ? "ERROR" : "UNKNOWN"));

    start_app_cpu(wired_init_task);
    mc_init_mem();

#ifndef CONFIG_BLUERETRO_BT_DISABLE
    if (bt_host_init()) {
        err_led_set();
        err = 1;
        printf("Bluetooth init fail!\n");
    }
#endif

    if (wired_adapter.system_id < WIRED_MAX) {
        wired_rtos_init();
    }

#ifndef CONFIG_BLUERETRO_QEMU
    int32_t mc_ret = mc_init();
    sys_mgr_init(chip_package);

    if (!nicemcu_stage5_display_init(sd_status, mc_ret == 0)) {
        printf("NiceMCU Stage 5.10 display init failed!\n");
    }
#endif

#ifdef CONFIG_BLUERETRO_WS_CMDS
    ws_srv_init();
#endif

    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (err) {
            printf("Boot error on pending img, rollback to the previous version.");
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
        else {
            printf("Pending img valid!");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
    vTaskDelete(NULL);
}

void app_main()
{
    adapter_init();
    xTaskCreatePinnedToCore(wl_init_task, "wl_init_task", 8192, NULL, 10, NULL, 0);
}
