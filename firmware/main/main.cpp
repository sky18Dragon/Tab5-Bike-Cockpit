/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <sys/time.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "private/utils.hpp"
#include "brookesia/lib_utils.hpp"
#include "brookesia/service_manager.hpp"
#include "brookesia/hal_interface.hpp"
#include "brookesia/hal_adaptor.hpp"
#include "modules/wifi_provisioning.hpp"
#include "modules/ai_agents.hpp"
#include "modules/general_services.hpp"
#include "modules/profiler.hpp"
#include "modules/display/display.hpp"

using namespace esp_brookesia;

/**
 * Sanitise the wall clock before any worker thread starts.
 *
 * After a USB-triggered reset (CHIP_USB_UART_RESET, e.g. right after flashing) the RTC-retained boot
 * time is not cleared, so the wall clock can come up with a garbage value. Seen on Tab5: year ~2048,
 * tv_sec > INT32_MAX. With such a value boost's interruptible sleep (used by the Brookesia
 * TaskScheduler workers) returns immediately and spins, which starves IDLE0 and trips the task
 * watchdog right after "All services added". SNTP corrects the time later.
 */
static void sanitize_wall_clock(void)
{
    constexpr time_t VALID_MIN = 1767225600;  // 2026-01-01 00:00:00 UTC
    constexpr time_t VALID_MAX = 2147483647;  // INT32_MAX, 2038-01-19

    struct timeval tv {};
    gettimeofday(&tv, nullptr);
    if ((tv.tv_sec >= VALID_MIN) && (tv.tv_sec <= VALID_MAX)) {
        return;
    }

    ESP_LOGW("Main", "Wall clock out of range (%lld), resetting to 2026-01-01", static_cast<long long>(tv.tv_sec));
    tv.tv_sec = VALID_MIN;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
}


/**
 * Lower the output drive strength of the Tab5 signals that run next to the backlight PWM, the DSI display
 * and the SDIO link. Ported from the official M5Tab5-UserDemo (HalEsp32::set_gpio_output_capability).
 *
 * Without it, switching the backlight on made the ST7121/ST7123 touch controller stop answering on I2C
 * and the ESP32-C6 SDIO handshake fail (sdmmc_init_ocr timeout) on our board.
 */
static void tab5_set_gpio_drive_capability(void)
{
    static const gpio_num_t TAB5_GPIOS[] = {
        GPIO_NUM_0, GPIO_NUM_1,                                                                  // EXT I2C
        GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_15, // ESP-Hosted C6
        GPIO_NUM_22, GPIO_NUM_23,                                                                // backlight, TP_INT
        GPIO_NUM_26, GPIO_NUM_27, GPIO_NUM_28, GPIO_NUM_29, GPIO_NUM_30,                         // audio
        GPIO_NUM_31, GPIO_NUM_32,                                                                // system I2C
        GPIO_NUM_39, GPIO_NUM_40, GPIO_NUM_41, GPIO_NUM_42, GPIO_NUM_43, GPIO_NUM_44,            // microSD
    };
    for (gpio_num_t gpio : TAB5_GPIOS) {
        if (gpio_set_drive_capability(gpio, GPIO_DRIVE_CAP_0) != ESP_OK) {
            ESP_LOGW("Main", "Failed to set drive capability of GPIO %d", static_cast<int>(gpio));
        }
    }
}

/* Drivers (ESP-Hosted SDIO, SD card) may re-initialise their pins later, so apply it again once boot settled. */
static void tab5_drive_capability_task(void *)
{
    for (int delay_ms : {3000, 3000, 4000}) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        tab5_set_gpio_drive_capability();
    }
    vTaskDelete(nullptr);
}

extern "C" void app_main(void)
{
    sanitize_wall_clock();
    tab5_set_gpio_drive_capability();
    xTaskCreate(tab5_drive_capability_task, "gpio_drv", 3072, nullptr, 2, nullptr);

    BROOKESIA_LOGI("\n\n=== Agent Chatbot Example ===\n");

    // Set log level to warning for some components to reduce log output
    esp_log_level_set("ESP_GMF_TASK", ESP_LOG_WARN);
    esp_log_level_set("ESP_GMF_FILE", ESP_LOG_WARN);
    esp_log_level_set("ESP_GMF_PORT", ESP_LOG_WARN);
    esp_log_level_set("ESP_GMF_POOL", ESP_LOG_WARN);
    esp_log_level_set("ESP_ES_PARSER", ESP_LOG_WARN);
    esp_log_level_set("GMF_AFE", ESP_LOG_WARN);
    esp_log_level_set("ESP_GMF_AENC", ESP_LOG_WARN);
    esp_log_level_set("AFE_MANAGER", ESP_LOG_WARN);
    esp_log_level_set("AUD_RENDER", ESP_LOG_WARN);
    esp_log_level_set("AUDIO_PROCESSOR", ESP_LOG_WARN);
    esp_log_level_set("AUD_SIMP_PLAYER", ESP_LOG_WARN);
    esp_log_level_set("AUD_SDEC", ESP_LOG_WARN);
    esp_log_level_set("AFE_CONFIG", ESP_LOG_WARN);
    esp_log_level_set("NEW_DATA_BUS", ESP_LOG_WARN);
    esp_log_level_set("ESP_XIAOZHI_MQTT", ESP_LOG_WARN);
    esp_log_level_set("ESP_XIAOZHI_CHAT", ESP_LOG_WARN);
    esp_log_level_set("AFE", ESP_LOG_ERROR);

    /* Initialize all devices from HAL adaptor */
#if CONFIG_SOC_CPU_CORES_NUM > 1
    {
        // For SPI LCDs, bus initialization and data transmission operations must be performed on the same core;
        // otherwise, a crash may occur.
        BROOKESIA_THREAD_CONFIG_GUARD({
            .core_id = 1,
        });
        std::thread([&]() {
            hal::init_device(hal::DisplayDevice::DEVICE_NAME);
        }).join();
    }
#endif
    hal::init_all_devices();

    /* Start ServiceManager */
    auto &service_manager = service::ServiceManager::get_instance();
    BROOKESIA_CHECK_FALSE_EXIT(service_manager.start(), "Failed to start ServiceManager");

    /* Create a task scheduler for backend usage */
    std::shared_ptr<lib_utils::TaskScheduler> backend_scheduler;
    BROOKESIA_CHECK_EXCEPTION_EXIT(
        backend_scheduler = std::make_shared<lib_utils::TaskScheduler>(), "Failed to create task scheduler"
    );
    auto start_result = backend_scheduler->start({
        .worker_configs = {
            {
                .name = "BackendWorker1",
                .core_id = 0,
                .priority = 1,
                .stack_size = 10 * 1024,
            },
            {
                .name = "BackendWorker2",
                .core_id = 1,
                .priority = 1,
                .stack_size = 10 * 1024,
            },
        }
    });
    BROOKESIA_CHECK_FALSE_EXIT(start_result, "Failed to start task scheduler");

    auto setup_task = [backend_scheduler]() {
        /* Initialize general services */
        GeneralServices::get_instance().init(backend_scheduler);
        GeneralServices::get_instance().start_nvs();
        GeneralServices::get_instance().start_sntp();
        GeneralServices::get_instance().start_device();
        GeneralServices::get_instance().init_audio();

        /* Initialize AI agents */
        AI_Agents::get_instance().init({
            .task_scheduler = backend_scheduler,
        });
        AI_Agents::get_instance().init_coze();
        AI_Agents::get_instance().init_openai();
        AI_Agents::get_instance().init_xiaozhi();

        /* Start display UI */
        Display::get_instance().start({
            .task_scheduler = backend_scheduler,
#if CONFIG_SOC_CPU_CORES_NUM > 1
            .lvgl_task_core = 1,
            .emote_task_core = 1,
#endif
        });

        /* Start WiFi provisioning module */
        WifiProvisioning::get_instance().init({
            .task_scheduler = backend_scheduler,
        });
        WifiProvisioning::get_instance().start();

        /* Start profiler */
        Profiler::get_instance().init({
            .task_scheduler = backend_scheduler,
            .thread_idle_cpu_usage_threshold = 1,
            .thread_stack_usage_threshold = 128,
            .mem_internal_largest_free_threshold = 10 * 1024,
            .mem_internal_free_percent_threshold = 10,
            .mem_external_largest_free_threshold = 500 * 1024,
            .mem_external_free_percent_threshold = 20,
        });
        Profiler::get_instance().start_thread_profiler(false);
        Profiler::get_instance().start_memory_profiler(false);
    };
    auto post_result = backend_scheduler->post(std::move(setup_task));
    BROOKESIA_CHECK_FALSE_EXIT(post_result, "Failed to post setup task");
}
