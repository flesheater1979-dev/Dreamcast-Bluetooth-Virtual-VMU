/* MODIFIED for Dreamcast Bluetooth + Virtual VMU NiceMCU Beta 0.9 (firmware Stage 5.10). See MODIFIED-FILES.md. */
/*
 * Copyright (c) 2021-2025, Jacques Gagnon
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <soc/soc_memory_layout.h>
#include "xtensa/core-macros.h"
#include "sdkconfig.h"
#include "system/fs.h"
#include "config.h"
#include "memory_card.h"
#include "nicemcu_stage5_storage.h"

/* Related to bare metal hack, something goes writing there. */
/* Workaround: Remove region from heap pool until I figure it out */
SOC_RESERVE_MEMORY_REGION(0x3FFE7D98, 0x3FFE7E28, bad_region);

static uint8_t *mc_buffer[MC_BUFFER_BLOCK_CNT] = {0};
static esp_timer_handle_t mc_timer_hdl = NULL;
static int32_t mc_block_state = 0;

/* NiceMCU Stage 5.9 UI telemetry only.  Keep Maple write hook RAM-only. */
volatile uint32_t nicemcu_vmu_write_seq = 0;
volatile uint32_t nicemcu_vmu_store_seq = 0;

static int32_t mc_restore(void);
static int32_t mc_store(void);
static int32_t mc_load_path(const char *path);
static int32_t mc_store_path(const char *path);
static int32_t mc_store_block_path(const char *path, uint32_t block);
static inline void mc_store_cb(void *arg);

static void mc_start_update_timer(uint64_t timeout_us) {
    if (mc_timer_hdl) {
        if (esp_timer_is_active(mc_timer_hdl)) {
            esp_timer_stop(mc_timer_hdl);
        }
        esp_timer_start_once(mc_timer_hdl, timeout_us);
    }
}

static int32_t mc_load_path(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        printf("# mc_load_path: failed to open %s\n", path);
        return -1;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < MC_BUFFER_BLOCK_CNT; i++) {
        count += fread((void *)mc_buffer[i], MC_BUFFER_BLOCK_SIZE, 1, file);
    }
    fclose(file);

    if (count == MC_BUFFER_BLOCK_CNT) {
        printf("# mc_restore: loaded %s\n", path);
        return 0;
    }

    printf("# mc_restore: failed %s cnt:%ld size:%ld\n", path, count, st.st_size);
    return -1;
}

static int32_t mc_store_path(const char *path) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        printf("# mc_store: failed to open %s\n", path);
        return -1;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < MC_BUFFER_BLOCK_CNT; i++) {
        count += fwrite((void *)mc_buffer[i], MC_BUFFER_BLOCK_SIZE, 1, file);
    }
    fflush(file);
    fclose(file);

    printf("# mc_store: %s cnt:%ld\n", path, count);
    return count == MC_BUFFER_BLOCK_CNT ? 0 : -1;
}

static int32_t mc_store_block_path(const char *path, uint32_t block) {
    FILE *file = fopen(path, "r+b");
    if (file == NULL) {
        if (mc_store_path(path) != 0) return -1;
        file = fopen(path, "r+b");
        if (!file) return -1;
    }

    fseek(file, block * MC_BUFFER_BLOCK_SIZE, SEEK_SET);
    uint32_t count = fwrite((void *)mc_buffer[block], MC_BUFFER_BLOCK_SIZE, 1, file);
    fflush(file);
    fclose(file);
    return count == 1 ? 0 : -1;
}

static int32_t mc_restore(void) {
    const char *primary = nicemcu_stage5_storage_mc_path();

    if (mc_load_path(primary) == 0) {
        return 0;
    }

    if (strcmp(primary, MEMORY_CARD_FILE) != 0 && mc_load_path(MEMORY_CARD_FILE) == 0) {
        printf("# mc_restore: recovered from internal mirror\n");
        (void)mc_store_path(primary);
        return 0;
    }

    printf("# mc_restore: no usable card image; creating blank 128KiB VMU image\n");
    return mc_store();
}

static int32_t mc_store(void) {
    const char *primary = nicemcu_stage5_storage_mc_path();
    int32_t ret = mc_store_path(primary);

    if (strcmp(primary, MEMORY_CARD_FILE) != 0) {
        int32_t mirror_ret = mc_store_path(MEMORY_CARD_FILE);
        if (ret != 0 && mirror_ret == 0) ret = 0;
    }

    if (ret == 0) mc_block_state = 0;
    return ret;
}

static int32_t mc_store_spread(void) {
    if (!mc_block_state) return 0;

    uint32_t bit = __builtin_ffs(mc_block_state);
    if (!bit) return 0;
    uint32_t block = bit - 1;
    const char *primary = nicemcu_stage5_storage_mc_path();

    int32_t primary_ret = mc_store_block_path(primary, block);
    int32_t mirror_ret = -1;

    if (strcmp(primary, MEMORY_CARD_FILE) != 0) {
        mirror_ret = mc_store_block_path(MEMORY_CARD_FILE, block);
    }

    if (primary_ret == 0 || mirror_ret == 0) {
        atomic_clear_bit(&mc_block_state, block);
                nicemcu_vmu_store_seq++;
        printf("# mc_store: block %ld synced%s\n",
               block, strcmp(primary, MEMORY_CARD_FILE) ? " SD+internal" : " internal");
    }
    else {
        printf("# mc_store: block %ld write failed\n", block);
        return -1;
    }

    if (mc_block_state) mc_start_update_timer(20000);
    return 0;
}

static inline void mc_store_cb(void *arg) {
    (void)mc_store_spread();
}

int32_t mc_init_mem(void) {
    for (uint32_t i = 0; i < MC_BUFFER_BLOCK_CNT; i++) {
        mc_buffer[i] = malloc(MC_BUFFER_BLOCK_SIZE);

        if (mc_buffer[i] == NULL) {
            printf("# %s mc_buffer[%ld] alloc fail\n", __FUNCTION__, i);
            heap_caps_dump_all();
            return -1;
        }
        memset(mc_buffer[i], 0xFF, MC_BUFFER_BLOCK_SIZE);
    }

    return 0;
}

int32_t mc_init(void) {
    int32_t ret = -1;

    if (config.global_cfg.banksel < CONFIG_BANKSEL_MAX) {
        const esp_timer_create_args_t mc_timer_args = {
            .callback = &mc_store_cb,
            .arg = (void *)NULL,
            .name = "mc_timer"
        };

        esp_timer_create(&mc_timer_args, &mc_timer_hdl);

        ret = mc_restore();
    }

    return ret;
}

void mc_storage_update(void) {
    mc_start_update_timer(1000000);
}

/* Assume r/w size will never cross blocks boundary */
void IRAM_ATTR mc_read(uint32_t addr, uint8_t *data, uint32_t size) {
    memcpy(data, mc_buffer[addr >> 12] + (addr & 0xFFF), size);
}

void IRAM_ATTR mc_write(uint32_t addr, uint8_t *data, uint32_t size) {
    struct raw_fb fb_data = {0};
    uint32_t block = addr >> 12;

    memcpy(mc_buffer[block] + (addr & 0xFFF), data, size);
    nicemcu_vmu_write_seq++;

    if (config.global_cfg.banksel < CONFIG_BANKSEL_MAX) {
        atomic_set_bit(&mc_block_state, block);

        fb_data.header.wired_id = 0;
        fb_data.header.type = FB_TYPE_MEM_WRITE;
        fb_data.header.data_len = 0;
        adapter_q_fb(&fb_data);
    }
}

uint8_t IRAM_ATTR *mc_get_ptr(uint32_t addr) {
    return mc_buffer[addr >> 12] + (addr & 0xFFF);
}
