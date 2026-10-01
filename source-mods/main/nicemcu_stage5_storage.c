/* NiceMCU Stage 5.4 microSD boot-backup storage.
 *
 * The NiceMCU display and BlueRetro leave little internal/DMA heap once
 * Bluetooth is running.  Keeping FAT/SD mounted at the same time is therefore
 * unnecessary risk for Maple timing and display stability.
 *
 * Stage 5.4 uses the microSD only during boot, before Bluetooth and the LCD:
 *   - mount SD on SPI2: CS5/SCLK18/MISO19/MOSI23
 *   - verify read/write using FAT 8.3-safe names
 *   - rotate /sd/VMU/MC.BIN -> /sd/VMU/BACKUP/PREV.BIN
 *   - copy the active internal /fs/mc.bin -> /sd/VMU/MC.BIN
 *   - if internal mc.bin is missing but SD MC.BIN exists, restore it
 *   - unmount SD and free SPI2/DMA resources
 *
 * Runtime VMU reads/writes continue to use /fs/mc.bin.  The next boot backs
 * up the previous session automatically.  The card is never formatted.
 */

#include "nicemcu_stage5_storage.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

#define SD_HOST       SPI2_HOST
#define SD_PIN_CS      5
#define SD_PIN_SCLK   18
#define SD_PIN_MISO   19
#define SD_PIN_MOSI   23

#define SD_MOUNT       "/sd"
#define SD_VMU_DIR     "/sd/VMU"
#define SD_BACKUP_DIR  "/sd/VMU/BACKUP"
#define SD_MC_FILE     "/sd/VMU/MC.BIN"
#define SD_PREV_FILE   "/sd/VMU/BACKUP/PREV.BIN"
#define INTERNAL_MC    "/fs/mc.bin"
#define SD_PROBE_FILE  "/sd/S54.TST"

static const char *TAG = "NiceMCU_SD54";
static bool s_last_sync_ok;
static sdmmc_card_t *s_card;

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static long file_size(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static bool copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        ESP_LOGW(TAG, "open source failed %s errno=%d (%s)", src, errno, strerror(errno));
        return false;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        ESP_LOGW(TAG, "open destination failed %s errno=%d (%s)", dst, errno, strerror(errno));
        fclose(in);
        return false;
    }

    uint8_t buf[512];
    bool ok = true;
    while (1) {
        size_t n = fread(buf, 1, sizeof(buf), in);
        if (n && fwrite(buf, 1, n, out) != n) {
            ESP_LOGW(TAG, "write failed %s errno=%d (%s)", dst, errno, strerror(errno));
            ok = false;
            break;
        }
        if (n < sizeof(buf)) {
            if (ferror(in)) ok = false;
            break;
        }
    }
    if (fflush(out) != 0) ok = false;
    fclose(out);
    fclose(in);
    return ok;
}

static bool make_dir(const char *path) {
    errno = 0;
    if (mkdir(path, 0775) == 0 || errno == EEXIST) return true;
    ESP_LOGW(TAG, "mkdir %s failed errno=%d (%s)", path, errno, strerror(errno));
    return false;
}

static bool probe_file(void) {
    static const uint8_t payload[] = "S54SDOK\r\n";
    uint8_t check[sizeof(payload)] = {0};

    FILE *f = fopen(SD_PROBE_FILE, "wb");
    if (!f) {
        ESP_LOGW(TAG, "probe create failed errno=%d (%s)", errno, strerror(errno));
        return false;
    }
    bool ok = fwrite(payload, 1, sizeof(payload), f) == sizeof(payload);
    ok = fflush(f) == 0 && ok;
    fclose(f);
    if (!ok) {
        remove(SD_PROBE_FILE);
        return false;
    }

    f = fopen(SD_PROBE_FILE, "rb");
    if (!f) {
        ESP_LOGW(TAG, "probe read-open failed errno=%d (%s)", errno, strerror(errno));
        remove(SD_PROBE_FILE);
        return false;
    }
    size_t nr = fread(check, 1, sizeof(check), f);
    fclose(f);
    remove(SD_PROBE_FILE);
    ok = nr == sizeof(check) && memcmp(payload, check, sizeof(payload)) == 0;
    ESP_LOGI(TAG, "microSD read/write probe: %s", ok ? "OK" : "FAILED");
    return ok;
}

static void shutdown_sd(void) {
    if (s_card) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT, s_card);
        s_card = NULL;
    }
    esp_err_t free_err = spi_bus_free(SD_HOST);
    if (free_err != ESP_OK && free_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "spi_bus_free: %s", esp_err_to_name(free_err));
    }
}

bool nicemcu_stage5_storage_init(void) {
    s_last_sync_ok = false;
    s_card = NULL;

    ESP_LOGI(TAG, "boot backup: CS5 CLK18 MISO19 MOSI23; card will not stay mounted");
    ESP_LOGI(TAG, "heap before SD: free=%u largest=%u DMA=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_PIN_MOSI,
        .miso_io_num = SD_PIN_MISO,
        .sclk_io_num = SD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 512,
    };

    esp_err_t err = spi_bus_initialize(SD_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD SPI init failed: %s", esp_err_to_name(err));
        return false;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_HOST;
    host.max_freq_khz = SDMMC_FREQ_PROBING;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = SD_PIN_CS;
    slot.host_id = SD_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 3,
        .allocation_unit_size = 4096,
    };

    err = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot, &mount, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "microSD mount failed: %s", esp_err_to_name(err));
        spi_bus_free(SD_HOST);
        return false;
    }

    ESP_LOGI(TAG, "microSD mounted for boot backup");
    sdmmc_card_print_info(stdout, s_card);

    bool ok = probe_file() && make_dir(SD_VMU_DIR) && make_dir(SD_BACKUP_DIR);
    if (!ok) {
        ESP_LOGW(TAG, "microSD boot-backup preparation failed");
        shutdown_sd();
        return false;
    }

    bool internal_exists = file_exists(INTERNAL_MC);
    bool sd_exists = file_exists(SD_MC_FILE);

    if (internal_exists) {
        if (sd_exists) {
            if (copy_file(SD_MC_FILE, SD_PREV_FILE)) {
                ESP_LOGI(TAG, "rotated previous SD VMU -> BACKUP/PREV.BIN (%ld bytes)",
                         file_size(SD_PREV_FILE));
            }
            else {
                ESP_LOGW(TAG, "could not rotate previous SD VMU backup");
                ok = false;
            }
        }

        if (copy_file(INTERNAL_MC, SD_MC_FILE)) {
            ESP_LOGI(TAG, "backed up internal VMU -> SD/VMU/MC.BIN (%ld bytes)",
                     file_size(SD_MC_FILE));
        }
        else {
            ESP_LOGW(TAG, "VMU backup to SD failed");
            ok = false;
        }
    }
    else if (sd_exists) {
        if (copy_file(SD_MC_FILE, INTERNAL_MC)) {
            ESP_LOGI(TAG, "internal VMU was missing; restored it from SD (%ld bytes)",
                     file_size(INTERNAL_MC));
        }
        else {
            ESP_LOGW(TAG, "restore from SD failed");
            ok = false;
        }
    }
    else {
        ESP_LOGI(TAG, "no VMU image exists yet; first Dreamcast format/save will create it internally");
    }

    shutdown_sd();
    s_last_sync_ok = ok;

    ESP_LOGI(TAG, "microSD unmounted; SPI2/DMA released before Bluetooth/LCD");
    ESP_LOGI(TAG, "heap after SD release: free=%u largest=%u DMA=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "boot backup result: %s", ok ? "OK" : "FAILED");
    return ok;
}

bool nicemcu_stage5_storage_sd_ready(void) {
    /* Means the boot backup/restore completed, not that the card is mounted. */
    return s_last_sync_ok;
}

const char *nicemcu_stage5_storage_mc_path(void) {
    /* Runtime VMU is deliberately internal; SD is only mounted at boot. */
    return INTERNAL_MC;
}
