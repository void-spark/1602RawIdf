#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_rom_gpio.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "nvs_flash.h"
#include "lwip/apps/sntp.h"
#include "wifi_helper.h"
#include "mqtt_helper.h"
#include "hd44780.h"

static const char *TAG = "app";

#define BTN_BOOT GPIO_NUM_0

#define GPIO_OUTPUT_IO_RS    GPIO_NUM_13
#define GPIO_OUTPUT_IO_E     GPIO_NUM_27

#define GPIO_OUTPUT_IO_D4    GPIO_NUM_26
#define GPIO_OUTPUT_IO_D5    GPIO_NUM_25
#define GPIO_OUTPUT_IO_D6    GPIO_NUM_33
#define GPIO_OUTPUT_IO_D7    GPIO_NUM_32

static const char* ota_url = "http://raspberrypi.fritz.box:8032/esp32/1602RawIdf.bin";

static hd44780_t lcd;

static TimerHandle_t buttonTimer;

static void buttonPressed() {
    ESP_ERROR_CHECK(hd44780_clear(&lcd));
    ESP_ERROR_CHECK(hd44780_puts(&lcd, "Button!"));

    mqttPublish("devices/receiver/doorbell/reset", "true", 4, 2, 0);
}

static void buttonTimerCallback(TimerHandle_t xTimer) { 
    int level = gpio_get_level(BTN_BOOT);

    // https://www.embedded.com/electronics-blogs/break-points/4024981/My-favorite-software-debouncers
    static uint16_t state = 0; // Current debounce status
    state=(state<<1) | !level | 0xe000;
    if(state==0xf000) {
        buttonPressed();
    }
}

static void ota_task(void * pvParameter) {
    ESP_LOGI(TAG, "Starting OTA update...");

    esp_http_client_config_t config = {};
    config.url = ota_url;

    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = &config;

    ESP_LOGI(TAG, "Attempting to download update from %s", config.url);
    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA Succeed, Rebooting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware Upgrades Failed");
    }
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static void subscribeTopics() {
    subscribeDevTopic("$update");
    subscribeTopic("devices/the1602");
}

static void handleMessage(const char* topic1, const char* topic2, const char* topic3, const char* data) {
    if(
        strcmp(topic1, "$update") == 0 && 
        topic2 == NULL && 
        topic3 == NULL
    ) {
        xTaskCreate(&ota_task, "ota_task", 8192, NULL, 5, NULL);
    }
}

static bool handleAnyMessage(const char* topic, const char* data) {

    if(strcmp(topic,"devices/the1602") == 0) {

        ESP_ERROR_CHECK(hd44780_gotoxy(&lcd, 0, 0));

        int pos = 0;
        while (*data != 0 && *data != '\n') {
            ESP_ERROR_CHECK(hd44780_putc(&lcd, *data));
            data++;
            pos++;
        }
        for(; pos < 16; pos++) {
            ESP_ERROR_CHECK(hd44780_putc(&lcd, ' '));
        }

        ESP_ERROR_CHECK(hd44780_gotoxy(&lcd, 0, 1));

        int pos2 = 0;
        if(*data == '\n') {
            data++;
            while (*data != 0) {
                ESP_ERROR_CHECK(hd44780_putc(&lcd, *data));
                data++;
                pos2++;
            }
        }
        for(; pos2 < 16; pos2++) {
            ESP_ERROR_CHECK(hd44780_putc(&lcd, ' '));
        }

        return true;
    }

    return false;
}

extern "C" void app_main() {

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    lcd.write_cb = NULL;
    lcd.font = HD44780_FONT_5X8;
    lcd.lines = 2;   
    lcd.pins.rs = GPIO_OUTPUT_IO_RS;
    lcd.pins.e  = GPIO_OUTPUT_IO_E;
    lcd.pins.d4 = GPIO_OUTPUT_IO_D4;
    lcd.pins.d5 = GPIO_OUTPUT_IO_D5;
    lcd.pins.d6 = GPIO_OUTPUT_IO_D6;
    lcd.pins.d7 = GPIO_OUTPUT_IO_D7;
    lcd.pins.bl = HD44780_NOT_USED;

    ESP_ERROR_CHECK(hd44780_init(&lcd)); 

    // Initialize WiFi
    wifiStart();

    ESP_ERROR_CHECK(hd44780_clear(&lcd));
    ESP_ERROR_CHECK(hd44780_puts(&lcd, "WiFi..."));


    ESP_LOGI(TAG, "Waiting for wifi");
    wifiWait();

    ESP_ERROR_CHECK(hd44780_clear(&lcd));
    ESP_ERROR_CHECK(hd44780_puts(&lcd, "SNTP..."));

    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    ESP_ERROR_CHECK(hd44780_clear(&lcd));
    ESP_ERROR_CHECK(hd44780_puts(&lcd, "MQTT..."));

    mqttStart(subscribeTopics, handleMessage, handleAnyMessage);

    ESP_LOGI(TAG, "Waiting for MQTT");

    mqttWait();

    esp_rom_gpio_pad_select_gpio(BTN_BOOT);
    gpio_set_direction(BTN_BOOT, GPIO_MODE_INPUT);
    buttonTimer = xTimerCreate("ButtonTimer", (5 / portTICK_PERIOD_MS), pdTRUE, (void *) 0, buttonTimerCallback);

    xTimerStart(buttonTimer, 0);

    char ready[16 + 1];
    snprintf(ready, sizeof(ready), "Ready! %lub", esp_get_minimum_free_heap_size());
    ESP_ERROR_CHECK(hd44780_clear(&lcd));
    ESP_ERROR_CHECK(hd44780_puts(&lcd, ready));

    printf("Minimum free heap size: %lu bytes\n", esp_get_minimum_free_heap_size());

}
