/* NiceMCU-32S-DEV 2.8in ST7789 Dreamcast UI for BlueRetro v25.04.
 * Stage 5.10 keeps the exact Stage 5.3 LCD bring-up and DMA path that is
 * physically proven on this board. It removes the diagnostic colour bars from
 * normal boot, adds a clean animated startup, reports the real phase-1 SD
 * backup result, and retains the proven Bluetooth + VMU activity feedback.
 * Maple GPIO21/GPIO22 are never touched here.
 */
#include "nicemcu_stage5_display.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "bluetooth/host.h"

#define LCD_HOST SPI3_HOST
#define LCD_WIDTH 240
#define LCD_HEIGHT 320
#define LCD_PIN_MOSI 13
#define LCD_PIN_SCLK 14
#define LCD_PIN_CS 15
#define LCD_PIN_DC 12
#define LCD_PIN_RST 2
#define LCD_PIN_BL 25
#define LCD_CLOCK_HZ (24 * 1000 * 1000)
#define STRIPE_ROWS 16
#define SPIRAL_X 50
#define SPIRAL_Y 72
#define SPIRAL_W 140
#define SPIRAL_H 140
#define SPIRAL_POINTS 420

static const char *TAG = "NiceMCU_S510";
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_flush_done;
static uint16_t *s_stripe;
static bool s_vmu_ready;
static int s_sd_status;

/* Updated by adapter/memory_card.c. The Maple-side write hook only increments
 * a DRAM counter; all display work stays here in the FreeRTOS task. */
extern volatile uint32_t nicemcu_vmu_write_seq;
extern volatile uint32_t nicemcu_vmu_store_seq;

static const uint8_t s_font[26][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x01,0x01,0x01,0x01,0x11,0x11,0x0E},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}
};

static const uint8_t s_spiral[SPIRAL_POINTS][2] = {
    {74,71},
    {74,72},
    {74,72},
    {74,72},
    {74,72},
    {74,72},
    {74,73},
    {74,73},
    {74,73},
    {74,73},
    {74,74},
    {74,74},
    {74,74},
    {74,74},
    {74,75},
    {74,75},
    {74,75},
    {74,75},
    {73,75},
    {73,76},
    {73,76},
    {73,76},
    {73,76},
    {73,77},
    {72,77},
    {72,77},
    {72,77},
    {72,77},
    {71,78},
    {71,78},
    {71,78},
    {71,78},
    {70,78},
    {70,78},
    {70,79},
    {69,79},
    {69,79},
    {69,79},
    {68,79},
    {68,79},
    {68,79},
    {67,79},
    {67,79},
    {67,79},
    {66,79},
    {66,79},
    {66,79},
    {65,79},
    {65,79},
    {64,79},
    {64,79},
    {64,78},
    {63,78},
    {63,78},
    {62,78},
    {62,78},
    {62,78},
    {61,77},
    {61,77},
    {61,77},
    {60,77},
    {60,76},
    {60,76},
    {59,76},
    {59,75},
    {59,75},
    {58,75},
    {58,74},
    {58,74},
    {58,73},
    {57,73},
    {57,72},
    {57,72},
    {57,72},
    {56,71},
    {56,71},
    {56,70},
    {56,70},
    {56,69},
    {56,69},
    {56,68},
    {56,67},
    {56,67},
    {56,66},
    {56,66},
    {56,65},
    {56,65},
    {56,64},
    {56,64},
    {56,63},
    {56,62},
    {57,62},
    {57,61},
    {57,61},
    {57,60},
    {57,60},
    {58,59},
    {58,59},
    {58,58},
    {59,58},
    {59,57},
    {60,57},
    {60,56},
    {60,56},
    {61,55},
    {61,55},
    {62,54},
    {62,54},
    {63,54},
    {64,53},
    {64,53},
    {65,53},
    {65,52},
    {66,52},
    {67,52},
    {67,51},
    {68,51},
    {69,51},
    {69,51},
    {70,51},
    {71,51},
    {71,51},
    {72,50},
    {73,50},
    {74,50},
    {74,50},
    {75,51},
    {76,51},
    {77,51},
    {77,51},
    {78,51},
    {79,51},
    {80,51},
    {80,52},
    {81,52},
    {82,52},
    {82,53},
    {83,53},
    {84,53},
    {85,54},
    {85,54},
    {86,55},
    {87,55},
    {87,56},
    {88,56},
    {88,57},
    {89,58},
    {89,58},
    {90,59},
    {91,60},
    {91,60},
    {91,61},
    {92,62},
    {92,62},
    {93,63},
    {93,64},
    {93,65},
    {94,66},
    {94,67},
    {94,67},
    {94,68},
    {95,69},
    {95,70},
    {95,71},
    {95,72},
    {95,73},
    {95,74},
    {95,75},
    {95,76},
    {95,77},
    {95,78},
    {94,78},
    {94,79},
    {94,80},
    {94,81},
    {93,82},
    {93,83},
    {93,84},
    {92,85},
    {92,86},
    {91,87},
    {91,87},
    {90,88},
    {90,89},
    {89,90},
    {88,91},
    {88,91},
    {87,92},
    {86,93},
    {85,94},
    {85,94},
    {84,95},
    {83,95},
    {82,96},
    {81,97},
    {80,97},
    {79,98},
    {78,98},
    {77,98},
    {76,99},
    {75,99},
    {74,99},
    {73,100},
    {72,100},
    {71,100},
    {70,100},
    {69,100},
    {67,100},
    {66,100},
    {65,100},
    {64,100},
    {63,100},
    {62,100},
    {61,100},
    {60,99},
    {58,99},
    {57,99},
    {56,98},
    {55,98},
    {54,98},
    {53,97},
    {52,97},
    {51,96},
    {50,95},
    {49,95},
    {48,94},
    {47,93},
    {46,93},
    {45,92},
    {44,91},
    {43,90},
    {43,89},
    {42,88},
    {41,87},
    {40,86},
    {40,85},
    {39,84},
    {38,83},
    {38,82},
    {37,81},
    {37,79},
    {36,78},
    {36,77},
    {36,76},
    {35,75},
    {35,73},
    {35,72},
    {35,71},
    {34,69},
    {34,68},
    {34,67},
    {34,66},
    {34,64},
    {35,63},
    {35,62},
    {35,60},
    {35,59},
    {35,58},
    {36,56},
    {36,55},
    {37,54},
    {37,52},
    {38,51},
    {38,50},
    {39,49},
    {40,48},
    {40,46},
    {41,45},
    {42,44},
    {43,43},
    {44,42},
    {45,41},
    {46,40},
    {47,39},
    {48,38},
    {49,37},
    {50,36},
    {51,35},
    {53,35},
    {54,34},
    {55,33},
    {57,33},
    {58,32},
    {59,31},
    {61,31},
    {62,31},
    {63,30},
    {65,30},
    {66,30},
    {68,29},
    {69,29},
    {71,29},
    {72,29},
    {74,29},
    {75,29},
    {77,29},
    {78,29},
    {80,29},
    {81,30},
    {83,30},
    {84,30},
    {86,31},
    {87,31},
    {89,32},
    {90,33},
    {92,33},
    {93,34},
    {95,35},
    {96,35},
    {97,36},
    {99,37},
    {100,38},
    {101,39},
    {102,40},
    {103,41},
    {105,43},
    {106,44},
    {107,45},
    {108,46},
    {109,48},
    {109,49},
    {110,50},
    {111,52},
    {112,53},
    {113,55},
    {113,56},
    {114,58},
    {114,60},
    {115,61},
    {115,63},
    {116,64},
    {116,66},
    {116,68},
    {116,70},
    {116,71},
    {116,73},
    {116,75},
    {116,76},
    {116,78},
    {116,80},
    {116,81},
    {115,83},
    {115,85},
    {115,87},
    {114,88},
    {113,90},
    {113,92},
    {112,93},
    {111,95},
    {111,96},
    {110,98},
    {109,99},
    {108,101},
    {107,102},
    {106,104},
    {104,105},
    {103,106},
    {102,108},
    {101,109},
    {99,110},
    {98,111},
    {96,112},
    {95,113},
    {93,114},
    {92,115},
    {90,116},
    {88,117},
    {87,118},
    {85,118},
    {83,119},
    {81,120},
    {80,120},
    {78,121},
    {76,121},
    {74,121},
    {72,122},
    {70,122},
    {68,122},
    {66,122},
    {65,122},
    {63,122},
    {61,122},
    {59,121},
    {57,121},
    {55,121},
    {53,120},
    {51,120},
    {49,119},
    {48,118},
    {46,118},
    {44,117},
    {42,116},
    {40,115},
    {39,114},
    {37,113},
    {35,112},
    {34,111},
    {32,109},
    {31,108},
    {29,107},
    {28,105},
    {27,104},
    {25,102},
    {24,101},
    {23,99},
    {22,97},
    {21,95},
    {20,94},
    {19,92},
    {18,90},
    {17,88},
    {16,86},
    {16,84},
    {15,82},
    {14,80},
    {14,78},
    {14,76},
    {13,74},
    {13,72},
    {13,70},
};

static uint16_t rgb565_be(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t c = (uint16_t)((((uint16_t)(r & 0xF8)) << 8) |
                            (((uint16_t)(g & 0xFC)) << 3) |
                            ((uint16_t)b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

static bool lcd_color_done(esp_lcd_panel_io_handle_t io,
                           esp_lcd_panel_io_event_data_t *edata,
                           void *ctx) {
    (void)io; (void)edata; (void)ctx;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &hp);
    return hp == pdTRUE;
}

static bool send_pixels(int x0, int y0, int x1, int y1) {
    if (esp_lcd_panel_draw_bitmap(s_panel, x0, y0, x1, y1, s_stripe) != ESP_OK) return false;
    return xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(1000)) == pdTRUE;
}

static bool draw_solid_rect(int x0, int y0, int x1, int y1, uint16_t color) {
    if (!s_panel || !s_stripe || !s_flush_done) return false;
    int width = x1 - x0;
    if (width <= 0 || width > LCD_WIDTH || y1 <= y0) return false;
    for (int y = y0; y < y1; y += STRIPE_ROWS) {
        int rows = y1 - y;
        if (rows > STRIPE_ROWS) rows = STRIPE_ROWS;
        size_t pixels = (size_t)width * (size_t)rows;
        for (size_t i = 0; i < pixels; ++i) s_stripe[i] = color;
        if (!send_pixels(x0, y, x1, y + rows)) return false;
    }
    return true;
}

static uint8_t glyph_row(char c, int row) {
    if (row < 0 || row >= 7) return 0;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return s_font[c - 'A'][row];
    if (c == ':') return (row == 2 || row == 5) ? 0x04 : 0;
    if (c == '-') return row == 3 ? 0x0E : 0;
    return 0;
}

static int text_width(const char *text, int scale) {
    return (int)strlen(text) * 6 * scale;
}

static bool draw_text(int x, int y, const char *text, int scale,
                      uint16_t fg, uint16_t bg) {
    int width = text_width(text, scale);
    int height = 7 * scale;
    if (width <= 0 || width > LCD_WIDTH || x < 0 || x + width > LCD_WIDTH) return false;
    for (int sy = 0; sy < height; sy += STRIPE_ROWS) {
        int rows = height - sy;
        if (rows > STRIPE_ROWS) rows = STRIPE_ROWS;
        for (int py = 0; py < rows; ++py) {
            int gy = (sy + py) / scale;
            for (int px = 0; px < width; ++px) {
                int char_cell = px / (6 * scale);
                int in_cell = px % (6 * scale);
                int gx = in_cell / scale;
                uint16_t c = bg;
                if (gx < 5) {
                    uint8_t bits = glyph_row(text[char_cell], gy);
                    if (bits & (1u << (4 - gx))) c = fg;
                }
                s_stripe[(size_t)py * (size_t)width + (size_t)px] = c;
            }
        }
        if (!send_pixels(x, y + sy, x + width, y + sy + rows)) return false;
    }
    return true;
}

static bool draw_text_centered(int y, const char *text, int scale,
                               uint16_t fg, uint16_t bg) {
    int w = text_width(text, scale);
    return draw_text((LCD_WIDTH - w) / 2, y, text, scale, fg, bg);
}

static bool render_spiral(int visible, int highlight);

static bool draw_sd_status(void) {
    const uint16_t black = rgb565_be(0,0,0);
    const uint16_t green = rgb565_be(90,230,125);
    const uint16_t orange = rgb565_be(255,110,0);
    const uint16_t dim = rgb565_be(95,95,105);
    const char *label = "SD:BACKUP UNKNOWN";
    uint16_t col = dim;

    if (s_sd_status == NICEMCU_SD_STATUS_OK) {
        label = "SD:BACKUP OK";
        col = green;
    }
    else if (s_sd_status == NICEMCU_SD_STATUS_ERROR) {
        label = "SD:BACKUP ERROR";
        col = orange;
    }

    if (!draw_solid_rect(10,236,230,253,black)) return false;
    return draw_text_centered(241, label, 1, col, black);
}

static bool draw_base_ui(void) {
    const uint16_t black = rgb565_be(0,0,0);
    const uint16_t white = rgb565_be(245,245,245);
    const uint16_t orange = rgb565_be(255,110,0);
    if (!draw_solid_rect(0,0,LCD_WIDTH,LCD_HEIGHT,black)) return false;
    if (!draw_solid_rect(18,52,222,54,orange)) return false;
    if (!draw_text_centered(20, "DREAMCAST", 2, white, black)) return false;
    if (!draw_text_centered(220, "BLUERETRO", 1, orange, black)) return false;
    if (!draw_sd_status()) return false;
    if (!draw_solid_rect(18,258,222,260,orange)) return false;
    return true;
}

static bool play_boot_animation(void) {
    const uint16_t black = rgb565_be(0,0,0);
    const uint16_t white = rgb565_be(245,245,245);
    const uint16_t orange = rgb565_be(255,110,0);
    const uint16_t dim = rgb565_be(120,120,130);

    if (!draw_solid_rect(0,0,LCD_WIDTH,LCD_HEIGHT,black)) return false;
    if (!draw_text_centered(25, "DREAMCAST", 2, white, black)) return false;
    if (!draw_text_centered(52, "BLUERETRO", 1, dim, black)) return false;

    for (int visible = 1; visible < SPIRAL_POINTS; visible += 18) {
        if (!render_spiral(visible, -1)) return false;
        vTaskDelay(pdMS_TO_TICKS(22));
    }
    if (!render_spiral(SPIRAL_POINTS, -1)) return false;
    if (!draw_text_centered(225, "READY", 1, orange, black)) return false;
    vTaskDelay(pdMS_TO_TICKS(300));
    return true;
}

static bool draw_bt_status(bool connected) {
    const uint16_t black = rgb565_be(0,0,0);
    const uint16_t white = rgb565_be(245,245,245);
    const uint16_t orange = rgb565_be(255,110,0);
    if (!draw_solid_rect(10,264,230,282,black)) return false;
    return draw_text_centered(269, connected ? "BT:CONNECTED" : "BT:PAIRING",
                              1, connected ? white : orange, black);
}

enum {
    VMU_UI_READY = 0,
    VMU_UI_WRITING,
    VMU_UI_SAVED,
};

static bool draw_vmu_status(int state) {
    const uint16_t black = rgb565_be(0,0,0);
    const uint16_t white = rgb565_be(245,245,245);
    const uint16_t orange = rgb565_be(255,110,0);
    const uint16_t green = rgb565_be(90,230,125);
    const uint16_t dim = rgb565_be(95,95,105);
    const char *label = "VMU:READY";
    uint16_t col = s_vmu_ready ? white : dim;

    if (!s_vmu_ready) {
        label = "VMU:NOT READY";
    }
    else if (state == VMU_UI_WRITING) {
        label = "VMU:WRITING";
        col = orange;
    }
    else if (state == VMU_UI_SAVED) {
        label = "VMU:SAVED";
        col = green;
    }

    if (!draw_solid_rect(10,286,230,310,black)) return false;
    return draw_text_centered(293, label, 1, col, black);
}

static bool render_spiral(int visible, int highlight) {
    const uint16_t black = rgb565_be(0,0,0);
    const uint16_t orange = rgb565_be(255,95,0);
    const uint16_t glow = rgb565_be(255,210,145);
    if (visible < 0) visible = 0;
    if (visible > SPIRAL_POINTS) visible = SPIRAL_POINTS;

    for (int sy = 0; sy < SPIRAL_H; sy += STRIPE_ROWS) {
        int rows = SPIRAL_H - sy;
        if (rows > STRIPE_ROWS) rows = STRIPE_ROWS;
        size_t pixels = (size_t)SPIRAL_W * (size_t)rows;
        for (size_t i = 0; i < pixels; ++i) s_stripe[i] = black;

        for (int p = 0; p < visible; ++p) {
            int px = s_spiral[p][0];
            int py = s_spiral[p][1];
            if (py < sy - 1 || py >= sy + rows + 1) continue;
            uint16_t col = (highlight >= 0 && p >= highlight && p < highlight + 18) ? glow : orange;
            for (int dy = -1; dy <= 1; ++dy) {
                int yy = py + dy - sy;
                if (yy < 0 || yy >= rows) continue;
                for (int dx = -1; dx <= 1; ++dx) {
                    int xx = px + dx;
                    if (xx < 0 || xx >= SPIRAL_W) continue;
                    s_stripe[(size_t)yy * SPIRAL_W + (size_t)xx] = col;
                }
            }
        }
        if (!send_pixels(SPIRAL_X, SPIRAL_Y + sy,
                         SPIRAL_X + SPIRAL_W, SPIRAL_Y + sy + rows)) return false;
    }
    return true;
}

static void ui_task(void *arg) {
    (void)arg;
    int frame = 0;
    bool last_connected = false;
    bool first_status = true;
    uint32_t last_write_seq = nicemcu_vmu_write_seq;
    uint32_t last_store_seq = nicemcu_vmu_store_seq;
    int vmu_state = VMU_UI_READY;
    int vmu_hold_frames = 0;

    for (;;) {
        int highlight = (frame * 9) % SPIRAL_POINTS;
        render_spiral(SPIRAL_POINTS, highlight);

        bool connected = bt_host_get_flag_dev_cnt(BT_DEV_HID_INIT_DONE) > 0;
        if (first_status || connected != last_connected) {
            draw_bt_status(connected);
            last_connected = connected;
            first_status = false;
        }

        uint32_t write_seq = nicemcu_vmu_write_seq;
        uint32_t store_seq = nicemcu_vmu_store_seq;
        int next_vmu_state = vmu_state;

        if (write_seq != last_write_seq) {
            last_write_seq = write_seq;
            next_vmu_state = VMU_UI_WRITING;
            vmu_hold_frames = 25; /* 2.5 s maximum if persistence has not fired yet */
        }
        if (store_seq != last_store_seq) {
            last_store_seq = store_seq;
            next_vmu_state = VMU_UI_SAVED;
            vmu_hold_frames = 15; /* make the persisted confirmation readable */
        }
        else if (vmu_hold_frames > 0) {
            vmu_hold_frames--;
        }
        else {
            next_vmu_state = VMU_UI_READY;
        }

        if (next_vmu_state != vmu_state) {
            vmu_state = next_vmu_state;
            draw_vmu_status(vmu_state);
        }

        frame++;
        if (frame >= SPIRAL_POINTS) frame = 0;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

bool nicemcu_stage5_display_init(int sd_status, bool vmu_ready) {
    s_sd_status = sd_status;
    s_vmu_ready = vmu_ready;
    esp_err_t err;
    ESP_LOGI(TAG, "Stage 5.10 LCD init - proven Stage 5.3 path + final UI");

    gpio_config_t bl = {
        .pin_bit_mask = 1ULL << LCD_PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if ((err = gpio_config(&bl)) != ESP_OK) {
        ESP_LOGE(TAG, "backlight config failed: %s", esp_err_to_name(err));
        return false;
    }
    gpio_set_level(LCD_PIN_BL, 0);

    s_flush_done = xSemaphoreCreateBinary();
    if (!s_flush_done) return false;

    const size_t stripe_pixels = (size_t)LCD_WIDTH * STRIPE_ROWS;
    s_stripe = heap_caps_malloc(stripe_pixels * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_stripe) return false;

    spi_bus_config_t bus = {
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)(stripe_pixels * sizeof(uint16_t)),
    };
    if ((err = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO)) != ESP_OK) {
        ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(err));
        return false;
    }

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_PIN_CS,
        .dc_gpio_num = LCD_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_CLOCK_HZ,
        .trans_queue_depth = 2,
        .on_color_trans_done = lcd_color_done,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    if ((err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io)) != ESP_OK) {
        ESP_LOGE(TAG, "panel IO failed: %s", esp_err_to_name(err));
        return false;
    }

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    if ((err = esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel)) != ESP_OK ||
        (err = esp_lcd_panel_reset(s_panel)) != ESP_OK ||
        (err = esp_lcd_panel_init(s_panel)) != ESP_OK ||
        (err = esp_lcd_panel_invert_color(s_panel, true)) != ESP_OK ||
        (err = esp_lcd_panel_disp_on_off(s_panel, true)) != ESP_OK) {
        ESP_LOGE(TAG, "ST7789 setup failed: %s", esp_err_to_name(err));
        return false;
    }

    /* Keep the physically-proven Stage 5.3 panel/SPI path, but normal boot is
     * now clean: draw black first, then enable the backlight and animate. */
    if (!draw_solid_rect(0,0,LCD_WIDTH,LCD_HEIGHT,rgb565_be(0,0,0))) return false;
    gpio_set_level(LCD_PIN_BL, 1);
    ESP_LOGI(TAG, "Stage 5.10 clean Dreamcast boot animation");
    if (!play_boot_animation()) return false;

    if (!draw_base_ui()) return false;
    if (!render_spiral(SPIRAL_POINTS, -1)) return false;
    if (!draw_bt_status(bt_host_get_flag_dev_cnt(BT_DEV_HID_INIT_DONE) > 0)) return false;
    if (!draw_vmu_status(VMU_UI_READY)) return false;

    if (xTaskCreate(ui_task, "nicemcu_ui", 3072, NULL, 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "UI task creation failed");
        return false;
    }

    ESP_LOGI(TAG, "Stage 5.10 UI started; SD=%s, BT live, VMU feedback enabled, VMU=%s",
             s_sd_status == NICEMCU_SD_STATUS_OK ? "BACKUP OK" :
             (s_sd_status == NICEMCU_SD_STATUS_ERROR ? "BACKUP ERROR" : "UNKNOWN"),
             s_vmu_ready ? "READY" : "NOT READY");
    return true;
}
