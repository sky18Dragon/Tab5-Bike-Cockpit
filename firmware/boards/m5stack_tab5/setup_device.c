/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_board_manager_includes.h"
#include "esp_io_expander_pi4ioe5v6408.h"
#include "esp_lcd_ili9881c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_st7121.h"
#include "esp_lcd_st7123.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_touch_st7123.h"
#include "disp_init_data.h"
#include "st7121_init_data.h"

static const char *TAG = "M5STACK_TAB5_SETUP_DEVICE";

#define TAB5_TOUCH_ADDR_GT911   0x28u  /* 8-bit / left-shifted */
#define TAB5_TOUCH_ADDR_ST712X  0xaau  /* 8-bit / left-shifted */
#define TAB5_ST712X_FW_CMD      0x0000u
#define TAB5_ST7121_FW_VERSION  1u
#define TAB5_ST7123_FW_VERSION  3u

typedef struct {
    lcd_rgb_element_order_t  data_endian;
    uint32_t                 dpi_clock_freq_mhz;
    uint8_t                  num_fbs;
    uint16_t                 hsync_back_porch;
    uint16_t                 hsync_pulse_width;
    uint16_t                 hsync_front_porch;
    uint16_t                 vsync_back_porch;
    uint16_t                 vsync_pulse_width;
    uint16_t                 vsync_front_porch;
} tab5_panel_timing_t;

typedef enum {
    TAB5_PANEL_VARIANT_ILI9881C,
    TAB5_PANEL_VARIANT_ST7121,
    TAB5_PANEL_VARIANT_ST7123,
} tab5_panel_variant_t;

static const tab5_panel_timing_t s_timing_ili9881c = {
    .data_endian        = LCD_RGB_DATA_ENDIAN_BIG,
    .dpi_clock_freq_mhz = 80,
    .num_fbs            = 1,
    .hsync_back_porch   = 140,
    .hsync_pulse_width  = 40,
    .hsync_front_porch  = 40,
    .vsync_back_porch   = 20,
    .vsync_pulse_width  = 4,
    .vsync_front_porch  = 20,
};

static const tab5_panel_timing_t s_timing_st7121 = {
    .data_endian        = LCD_RGB_DATA_ENDIAN_LITTLE,
    .dpi_clock_freq_mhz = 70,
    .num_fbs            = 1,
    .hsync_back_porch   = 40,
    .hsync_pulse_width  = 2,
    .hsync_front_porch  = 40,
    .vsync_back_porch   = 24,
    .vsync_pulse_width  = 20,
    .vsync_front_porch  = 200,
};

static const tab5_panel_timing_t s_timing_st7123 = {
    .data_endian        = LCD_RGB_DATA_ENDIAN_LITTLE,
    .dpi_clock_freq_mhz = 80,
    .num_fbs            = 2,
    .hsync_back_porch   = 40,
    .hsync_pulse_width  = 2,
    .hsync_front_porch  = 40,
    .vsync_back_porch   = 8,
    .vsync_pulse_width  = 2,
    .vsync_front_porch  = 220,
};

static void apply_timing(dev_display_lcd_config_t *cfg, const tab5_panel_timing_t *t)
{
    cfg->data_endian = t->data_endian;
    cfg->sub_cfg.dsi.dpi_config.dpi_clock_freq_mhz = t->dpi_clock_freq_mhz;
    cfg->sub_cfg.dsi.dpi_config.num_fbs = t->num_fbs;
    cfg->sub_cfg.dsi.dpi_config.video_timing.hsync_back_porch = t->hsync_back_porch;
    cfg->sub_cfg.dsi.dpi_config.video_timing.hsync_pulse_width = t->hsync_pulse_width;
    cfg->sub_cfg.dsi.dpi_config.video_timing.hsync_front_porch = t->hsync_front_porch;
    cfg->sub_cfg.dsi.dpi_config.video_timing.vsync_back_porch = t->vsync_back_porch;
    cfg->sub_cfg.dsi.dpi_config.video_timing.vsync_pulse_width = t->vsync_pulse_width;
    cfg->sub_cfg.dsi.dpi_config.video_timing.vsync_front_porch = t->vsync_front_porch;
}

static tab5_panel_variant_t detect_panel_variant(uint16_t touch_addr)
{
    if (touch_addr != TAB5_TOUCH_ADDR_ST712X) {
        return TAB5_PANEL_VARIANT_ILI9881C;
    }

    dev_lcd_touch_handles_t *touch_handles = NULL;
    esp_err_t ret = esp_board_device_get_handle("lcd_touch", (void **)&touch_handles);
    if (ret != ESP_OK || !touch_handles || !touch_handles->io_handle) {
        ESP_LOGW(TAG, "ST712x touch handle unavailable, defaulting to ST7123: %s", esp_err_to_name(ret));
        return TAB5_PANEL_VARIANT_ST7123;
    }

    uint8_t fw_version = 0;
    ret = esp_lcd_panel_io_rx_param(touch_handles->io_handle, TAB5_ST712X_FW_CMD, &fw_version, sizeof(fw_version));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read ST712x touch firmware version, defaulting to ST7123: %s", esp_err_to_name(ret));
        return TAB5_PANEL_VARIANT_ST7123;
    }

    if (fw_version == TAB5_ST7121_FW_VERSION) {
        ESP_LOGI(TAG, "Detected ST712x touch firmware version %u, using ST7121 display", fw_version);
        return TAB5_PANEL_VARIANT_ST7121;
    }
    if (fw_version == TAB5_ST7123_FW_VERSION) {
        ESP_LOGI(TAG, "Detected ST712x touch firmware version %u, using ST7123 display", fw_version);
        return TAB5_PANEL_VARIANT_ST7123;
    }

    ESP_LOGW(TAG, "Unknown ST712x touch firmware version %u, defaulting to ST7123", fw_version);
    return TAB5_PANEL_VARIANT_ST7123;
}


/* ---- PI4IOE5V6408 bring-up, ported from the official M5Tab5-UserDemo BSP (m5stack_tab5.c) ---------
 *
 * Why this exists: the stock esp_board_manager board never resets the touch controller, so after a cold
 * start the ST712x touch was not found and the whole display init was aborted. The official BSP resets
 * both expanders, then pulses LCD_RST and TP_RST low together with the touch INT pin (GPIO 23) floating,
 * and enables charging (expander 0x44, P7).
 *
 * LCD_RST (expander 0x43, P4) is driven push-pull high, as in the released factory firmware (UserDemo
 * V0.4). UserDemo master (fix-lcd-rst, not released yet) releases it as "input + pull-up" instead; on our
 * unit every firmware that did so left the panel black, see docs/08.
 */
#define PI4IO_ADDR_0            0x43u   /* expander 0: SPK_EN, EXT5V_EN, LCD_RST, TP_RST, CAM_RST, HP detect */
#define PI4IO_ADDR_1            0x44u   /* expander 1: WLAN_PWR_EN, USB5V_EN, QC_EN, CHG_EN, ...            */
#define PI4IO_REG_CHIP_RESET    0x01u
#define PI4IO_REG_IO_DIR        0x03u
#define PI4IO_REG_OUT_SET       0x05u
#define PI4IO_REG_OUT_H_IM      0x07u
#define PI4IO_REG_IN_DEF_STA    0x09u
#define PI4IO_REG_PULL_EN       0x0Bu
#define PI4IO_REG_PULL_SEL      0x0Du
#define PI4IO_REG_INT_MASK      0x11u
#define TAB5_LCD_RST_PIN        4       /* expander 0, P4 */
#define TAB5_TP_RST_PIN         5       /* expander 0, P5 */
/* Extra wait before the first DCS command, on top of the 100 ms after the reset pulse. The factory
 * firmware has other init in between; Espressif's esp-bsp waits 500 ms. */
#define TAB5_LCD_RESET_SETTLE_MS 500
#define PI4IO_I2C_TIMEOUT_MS    100
#define TAB5_TP_INT_GPIO        GPIO_NUM_23

static esp_err_t pi4io_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, sizeof(buf), PI4IO_I2C_TIMEOUT_MS);
}

static esp_err_t pi4io_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(dev, &reg, 1, val, 1, PI4IO_I2C_TIMEOUT_MS);
}

/* Expander 0, following UserDemo V0.4 (bsp_io_expander_pi4ioe_init + bsp_reset_tp): reset the chip, all
 * used pins push-pull outputs, then LCD_RST and TP_RST low for 100 ms and high again. */
static esp_err_t tab5_expander0_bringup(i2c_master_dev_handle_t dev)
{
    esp_err_t ret = ESP_OK;
    uint8_t cur = 0;

    ret |= pi4io_write(dev, PI4IO_REG_CHIP_RESET, 0xFF);
    ret |= pi4io_read(dev, PI4IO_REG_CHIP_RESET, &cur);
    ret |= pi4io_write(dev, PI4IO_REG_IO_DIR, 0x7F);     /* P0..P6 output, P7 (HP detect) input */
    ret |= pi4io_write(dev, PI4IO_REG_OUT_H_IM, 0x00);   /* no pin left high-impedance */
    ret |= pi4io_write(dev, PI4IO_REG_PULL_SEL, 0x7F);
    ret |= pi4io_write(dev, PI4IO_REG_PULL_EN, 0x7F);
    ret |= pi4io_write(dev, PI4IO_REG_OUT_SET, 0x76);    /* P1 SPK_EN, P2 EXT5V_EN, P4 LCD_RST, P5 TP_RST, P6 CAM_RST */

    /* bsp_reset_tp: INT left floating, LCD_RST and TP_RST low for 100 ms, then high */
    gpio_reset_pin(TAB5_TP_INT_GPIO);
    ret |= pi4io_read(dev, PI4IO_REG_OUT_SET, &cur);
    ret |= pi4io_write(dev, PI4IO_REG_OUT_SET, cur & (uint8_t)~((1u << TAB5_LCD_RST_PIN) | (1u << TAB5_TP_RST_PIN)));
    vTaskDelay(pdMS_TO_TICKS(100));
    ret |= pi4io_read(dev, PI4IO_REG_OUT_SET, &cur);
    ret |= pi4io_write(dev, PI4IO_REG_OUT_SET, cur | (1u << TAB5_LCD_RST_PIN) | (1u << TAB5_TP_RST_PIN));
    vTaskDelay(pdMS_TO_TICKS(100));

    return (ret == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/* Expander 1: reset and configure; P0 WLAN_PWR_EN=1, P3 USB5V_EN=1, P7 CHG_EN=0 here (charging is
 * enabled later via the board YAML, matching the official setChargeEnable(true) after init). */
static esp_err_t tab5_expander1_bringup(i2c_master_dev_handle_t dev)
{
    esp_err_t ret = ESP_OK;
    uint8_t cur = 0;

    ret |= pi4io_write(dev, PI4IO_REG_CHIP_RESET, 0xFF);
    ret |= pi4io_read(dev, PI4IO_REG_CHIP_RESET, &cur);
    ret |= pi4io_write(dev, PI4IO_REG_IO_DIR, 0xB9);     /* 1 = output: P0 P3 P4 P5 P7; input: P1 P2 P6 */
    ret |= pi4io_write(dev, PI4IO_REG_OUT_H_IM, 0x06);
    ret |= pi4io_write(dev, PI4IO_REG_PULL_SEL, 0xB9);
    ret |= pi4io_write(dev, PI4IO_REG_PULL_EN, 0xF9);
    ret |= pi4io_write(dev, PI4IO_REG_IN_DEF_STA, 0x40);
    ret |= pi4io_write(dev, PI4IO_REG_INT_MASK, 0xBF);
    ret |= pi4io_write(dev, PI4IO_REG_OUT_SET, 0x09);

    return (ret == ESP_OK) ? ESP_OK : ESP_FAIL;
}

static void tab5_expander_bringup(i2c_master_bus_handle_t bus, uint16_t dev_addr)
{
    if ((dev_addr != PI4IO_ADDR_0) && (dev_addr != PI4IO_ADDR_1)) {
        return;
    }

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = 400000,
    };
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t ret = i2c_master_bus_add_device(bus, &cfg, &dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Expander 0x%02x: add device failed: %s", dev_addr, esp_err_to_name(ret));
        return;
    }

    ret = (dev_addr == PI4IO_ADDR_0) ? tab5_expander0_bringup(dev) : tab5_expander1_bringup(dev);
    ESP_LOGI(TAG, "Expander 0x%02x bring-up %s", dev_addr, (ret == ESP_OK) ? "done" : "had I2C errors");
    i2c_master_bus_rm_device(dev);
}

esp_err_t io_expander_factory_entry_t(i2c_master_bus_handle_t i2c_handle,
                                      const uint16_t dev_addr,
                                      esp_io_expander_handle_t *handle_ret)
{
    esp_err_t ret = esp_io_expander_new_i2c_pi4ioe5v6408(i2c_handle, dev_addr, handle_ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create IO expander: %s", esp_err_to_name(ret));
        return ret;
    }

    /* The driver's constructor resets the chip and leaves every pin Hi-Z with a pull-down, which holds
     * LCD_RST and TP_RST low. Run the official bring-up *after* it so the reset lines end up released
     * the way the factory firmware does. The YAML lists P4 as an output at level 1, so the driver's later
     * writes of its cached output register keep LCD_RST high. */
    tab5_expander_bringup(i2c_handle, dev_addr);
    return ESP_OK;
}

esp_err_t lcd_dsi_panel_factory_entry_t(esp_lcd_dsi_bus_handle_t dsi_handle,
                                        dev_display_lcd_config_t *lcd_cfg,
                                        dev_display_lcd_handles_t *lcd_handles)
{
    vTaskDelay(pdMS_TO_TICKS(TAB5_LCD_RESET_SETTLE_MS));

    uint16_t touch_addr = 0;
    esp_err_t ret = esp_board_device_get_i2c_effective_addr("lcd_touch", &touch_addr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "lcd_touch effective addr unavailable: %s", esp_err_to_name(ret));
        return ret;
    }

    dev_display_lcd_config_t local = *lcd_cfg;
    tab5_panel_variant_t panel_variant = detect_panel_variant(touch_addr);
    const tab5_panel_timing_t *timing = &s_timing_ili9881c;
    if (panel_variant == TAB5_PANEL_VARIANT_ST7121) {
        timing = &s_timing_st7121;
    } else if (panel_variant == TAB5_PANEL_VARIANT_ST7123) {
        timing = &s_timing_st7123;
    }
    apply_timing(&local, timing);
    /* Project override: Brookesia GUI renders RGB565 with 3 frame buffers (tear avoidance) on every
     * panel variant, so keep the YAML values instead of the stock RGB888 / per-panel frame buffer count. */
    local.bits_per_pixel = lcd_cfg->bits_per_pixel;
    local.sub_cfg.dsi.dpi_config.num_fbs = lcd_cfg->sub_cfg.dsi.dpi_config.num_fbs;

    esp_lcd_panel_dev_config_t dev_config = {
        .reset_gpio_num = local.sub_cfg.dsi.reset_gpio_num,
        .rgb_ele_order = local.rgb_ele_order,
        .bits_per_pixel = local.bits_per_pixel,
        .data_endian = local.data_endian,
        .flags = {
            .reset_active_high = local.sub_cfg.dsi.reset_active_high,
        },
    };

    if (panel_variant == TAB5_PANEL_VARIANT_ST7121) {
        ESP_LOGI(TAG, "Tab5 panel variant: ST7121");
        st7121_vendor_config_t vendor = {
            .init_cmds = tab5_st7121_init_cmds,
            .init_cmds_size = sizeof(tab5_st7121_init_cmds) / sizeof(tab5_st7121_init_cmds[0]),
            .mipi_config = {
                .dsi_bus = dsi_handle,
                .dpi_config = &local.sub_cfg.dsi.dpi_config,
            },
        };
        dev_config.vendor_config = &vendor;
        ret = esp_lcd_new_panel_st7121(lcd_handles->io_handle, &dev_config, &lcd_handles->panel_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create st7121 panel: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    if (panel_variant == TAB5_PANEL_VARIANT_ST7123) {
        ESP_LOGI(TAG, "Tab5 panel variant: ST7123");
        st7123_vendor_config_t vendor = {
            .init_cmds = tab5_st7123_init_cmds,
            .init_cmds_size = sizeof(tab5_st7123_init_cmds) / sizeof(tab5_st7123_init_cmds[0]),
            .mipi_config = {
                .dsi_bus = dsi_handle,
                .dpi_config = &local.sub_cfg.dsi.dpi_config,
            },
        };
        dev_config.vendor_config = &vendor;
        ret = esp_lcd_new_panel_st7123(lcd_handles->io_handle, &dev_config, &lcd_handles->panel_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create st7123 panel: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    ESP_LOGI(TAG, "Tab5 panel variant: ILI9881C (touch addr=0x%02x)", touch_addr);
    ili9881c_vendor_config_t vendor = {
        .init_cmds = tab5_ili9881c_init_cmds,
        .init_cmds_size = sizeof(tab5_ili9881c_init_cmds) / sizeof(tab5_ili9881c_init_cmds[0]),
        .mipi_config = {
            .dsi_bus = dsi_handle,
            .dpi_config = &local.sub_cfg.dsi.dpi_config,
        },
    };
    dev_config.vendor_config = &vendor;
    ret = esp_lcd_new_panel_ili9881c(lcd_handles->io_handle, &dev_config, &lcd_handles->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ili9881c panel: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t lcd_touch_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_touch_config_t *touch_dev_config,
                                    esp_lcd_touch_handle_t *ret_touch)
{
    uint16_t touch_addr = 0;
    esp_err_t ret = esp_board_device_get_i2c_effective_addr("lcd_touch", &touch_addr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "lcd_touch effective addr unavailable: %s", esp_err_to_name(ret));
        return ret;
    }

    if (touch_addr == TAB5_TOUCH_ADDR_ST712X) {
        ret = esp_lcd_touch_new_i2c_st7123(io, touch_dev_config, ret_touch);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create st7123 touch: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    ret = esp_lcd_touch_new_i2c_gt911(io, touch_dev_config, ret_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create gt911 touch: %s", esp_err_to_name(ret));
    }
    return ret;
}
