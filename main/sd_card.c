#include <driver/sdmmc_host.h>
#include <driver/sdspi_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

#include "sd_card.h"

static const char *TAG = "ESP32-TFA-TH/SD";

sdmmc_card_t *s_card;

void init_sd_card() {
    sdmmc_host_t host_config = SDSPI_HOST_DEFAULT();
    host_config.slot = VSPI_HOST;
    host_config.max_freq_khz -= 1;

    sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
    slot_config.gpio_miso = VSPI_IOMUX_PIN_NUM_MISO;
    slot_config.gpio_mosi = VSPI_IOMUX_PIN_NUM_MOSI;
    slot_config.gpio_sck = VSPI_IOMUX_PIN_NUM_CLK;
    slot_config.gpio_cs = VSPI_IOMUX_PIN_NUM_CS;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = true,
            .max_files = 5,
            .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI(TAG, "Setting up SD card via SPI%d (MISO %d, MOSI %d, SCLK %d, CS %d) with frequency %d Hz",
             host_config.slot, slot_config.gpio_miso, slot_config.gpio_mosi, slot_config.gpio_sck, slot_config.gpio_cs,
             host_config.max_freq_khz);
    ESP_ERROR_CHECK(esp_vfs_fat_sdmmc_mount("/sdcard", &host_config, &slot_config, &mount_config, &s_card));
    sdmmc_card_print_info(stdout, s_card);
}
