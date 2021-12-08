#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "nvs_flash.h"
#include "lwip/apps/sntp.h"
#include "wifi_helper.h"
#include "mqtt_helper.h"
#include "hd44780.h"

static const char *TAG = "app";

#define GPIO_OUTPUT_IO_RS    GPIO_NUM_13
#define GPIO_OUTPUT_IO_E     GPIO_NUM_27

#define GPIO_OUTPUT_IO_D4    GPIO_NUM_26
#define GPIO_OUTPUT_IO_D5    GPIO_NUM_25
#define GPIO_OUTPUT_IO_D6    GPIO_NUM_33
#define GPIO_OUTPUT_IO_D7    GPIO_NUM_32

static const char* ota_url = "http://raspberrypi.fritz.box:8032/esp32/1602RawIdf.bin";

static hd44780_t lcd;

static void ota_task(void * pvParameter) {
    ESP_LOGI(TAG, "Starting OTA update...");

    esp_http_client_config_t config = {};
    config.url = ota_url;

    esp_err_t ret = esp_https_ota(&config);
    if (ret == ESP_OK) {
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
        ESP_ERROR_CHECK(hd44780_puts(&lcd, data));

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

    ESP_ERROR_CHECK(hd44780_gotoxy(&lcd, 0, 1));
    ESP_ERROR_CHECK(hd44780_puts(&lcd, "Connecting.."));


    ESP_LOGI(TAG, "Waiting for wifi");
    wifiWait();

    ESP_ERROR_CHECK(hd44780_gotoxy(&lcd, 0, 1));
    ESP_ERROR_CHECK(hd44780_puts(&lcd, "Connected!"));

    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    mqttStart(subscribeTopics, handleMessage, handleAnyMessage);

    ESP_LOGI(TAG, "Waiting for MQTT");

    mqttWait();


    printf("Minimum free heap size: %d bytes\n", esp_get_minimum_free_heap_size());
}
