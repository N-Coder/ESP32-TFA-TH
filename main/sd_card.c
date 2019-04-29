#include <driver/sdmmc_defs.h>
#include <driver/sdmmc_host.h>
#include <driver/sdspi_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <sdmmc/sdmmc_common.h>

#include "sd_card.h"

static const char *TAG = "ESP32-TFA-TH/SD";

sdmmc_host_t host_config = SDSPI_HOST_DEFAULT();
sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
};

sdmmc_card_t *s_card = NULL;

void set_sdmmc_log_level(esp_log_level_t level) {
    // esp_log_level_set("vfs_fat", level);
    esp_log_level_set("sdmmc_cmd", level);
    esp_log_level_set("sdspi_host", level);
    esp_log_level_set("sdspi_transaction", level);
}

void init_sd_card() {
    host_config.slot = VSPI_HOST;
    host_config.max_freq_khz -= 1; // "spi_master: SPI2: New device added to CS0, effective clock: 400kHz"

    slot_config.gpio_miso = VSPI_IOMUX_PIN_NUM_MISO;
    slot_config.gpio_mosi = VSPI_IOMUX_PIN_NUM_MOSI;
    slot_config.gpio_sck = VSPI_IOMUX_PIN_NUM_CLK;
    slot_config.gpio_cs = VSPI_IOMUX_PIN_NUM_CS;

    ESP_LOGI(TAG, "Set up SD card via SPI%d (MISO %d, MOSI %d, SCLK %d, CS %d) with frequency %d Hz",
             host_config.slot, slot_config.gpio_miso, slot_config.gpio_mosi, slot_config.gpio_sck, slot_config.gpio_cs,
             host_config.max_freq_khz);
}

esp_err_t ensure_sd_available(TickType_t timeout_ticks) {
    esp_err_t err;
    TickType_t startTime = xTaskGetTickCount();
    if (s_card != NULL) {
        err = sdmmc_init_cid(s_card);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SD Card removed, unmounting and waiting for remount: %s (%d)", esp_err_to_name(err), err);
            set_sdmmc_log_level(ESP_LOG_VERBOSE);
            esp_vfs_fat_sdmmc_unmount();
            s_card = NULL;
        }
    } else {
        err = ESP_ERR_INVALID_STATE;
        ESP_LOGD(TAG, "Trying to mount the SD card for the first time");
    }

    TickType_t lastWakeTime = xTaskGetTickCount();
    while (err != ESP_OK && (timeout_ticks == portMAX_DELAY || lastWakeTime < startTime + timeout_ticks)) {
        err = esp_vfs_fat_sdmmc_mount("/sdcard", &host_config, &slot_config, &mount_config, &s_card);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "SD Card mounted!");
            sdmmc_card_print_info(stdout, s_card);
            set_sdmmc_log_level(ESP_LOG_INFO);
            break;
        } else {
            vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1000));
            continue;
        }
    }
    return err;
}