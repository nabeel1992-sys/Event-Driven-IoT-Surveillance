#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "esp_camera.h"

static const char *TAG = "SECURITY_BRAIN";

// --- WiFi & Azure Config ---
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASS      "YOUR_WIFI_PASSWORD"
static const char *IOTHUB_HOST = "YOUR_IOTHUB_NAME.azure-devices.net";
static const char *DEVICE_ID   = "ESP32_S3_Cam";
static const char *MQTT_PASSWORD_SAS = "YOUR_AZURE_SAS_TOKEN";

// --- GoooouuTech Pins ---
#define XCLK_GPIO_NUM  15 
#define SIOD_GPIO_NUM  4 
#define SIOC_GPIO_NUM  5 
#define Y9_GPIO_NUM    16 
#define Y8_GPIO_NUM    17 
#define Y7_GPIO_NUM    18 
#define Y6_GPIO_NUM    12 
#define Y5_GPIO_NUM    10 
#define Y4_GPIO_NUM    8  
#define Y3_GPIO_NUM    9  
#define Y2_GPIO_NUM    11 
#define VSYNC_GPIO_NUM 6  
#define HREF_GPIO_NUM  7  
#define PCLK_GPIO_NUM  13 

// --- UART Config ---
#define TX_PIN 2
#define RX_PIN 1
#define BUF_SIZE 1024

static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
static esp_mqtt_client_handle_t mqtt_client = NULL;
static esp_netif_t* sta_netif = NULL; 

// --- 1. WiFi Logic ---
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
}

static void init_wifi(void) {
    if (nvs_flash_init() != ESP_OK) { nvs_flash_erase(); nvs_flash_init(); }
    esp_netif_init();
    if (esp_event_loop_create_default() != ESP_OK) { }
    if (sta_netif == NULL) sta_netif = esp_netif_create_default_wifi_sta(); 
    wifi_event_group = xEventGroupCreate();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);
    wifi_config_t wifi_config = {
        .sta = { .ssid = WIFI_SSID, .password = WIFI_PASS, 
                 .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                 .pmf_cfg = { .capable = true, .required = false } },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_start();
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}

// --- 2. Camera Logic ---
static esp_err_t init_camera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = -1;
    config.pin_reset = -1;
    config.xclk_freq_hz = 5000000; 
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    return esp_camera_init(&config);
}

// --- 3. MQTT Logic & Image Capture ---
static void start_mqtt(void) {
    char MQTT_USERNAME[256]; char MQTT_BROKER_URI[256];
    snprintf(MQTT_USERNAME, sizeof(MQTT_USERNAME), "%s/%s/?api-version=2021-04-12", IOTHUB_HOST, DEVICE_ID);
    snprintf(MQTT_BROKER_URI, sizeof(MQTT_BROKER_URI), "mqtts://%s:8883", IOTHUB_HOST);
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI, .credentials.client_id = DEVICE_ID,
        .credentials.username = MQTT_USERNAME, .credentials.authentication.password = MQTT_PASSWORD_SAS,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(mqtt_client);
}

void capture_and_send() {
    ESP_LOGI(TAG, "Capturing Photo for Azure...");
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        return;
    }
    char topic[128];
    snprintf(topic, sizeof(topic), "devices/%s/messages/events/", DEVICE_ID);
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, (const char *)fb->buf, fb->len, 1, 0);
    if (msg_id != -1) ESP_LOGW(TAG, "Image sent successfully to Azure!");
    esp_camera_fb_return(fb);
}

// --- 4. Main App ---
void app_main(void) {
    // A. Hard Reset Camera (GPIO 3 Power Cycle)
    gpio_reset_pin(3);
    gpio_set_direction(3, GPIO_MODE_OUTPUT);
    gpio_set_level(3, 0); 
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(3, 1); 
    vTaskDelay(pdMS_TO_TICKS(1000)); 

    // B. Camera Init
    if (init_camera() != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed! Check ribbon cable.");
    } else {
        ESP_LOGI(TAG, "Camera Initialized! ✅");
    }

    // C. WiFi & MQTT (After IP initialization, MQTT will start)
    init_wifi(); 
    ESP_LOGI(TAG, "Starting MQTT...");
    start_mqtt();

    // D. UART Init
    uart_config_t uart_config = { .baud_rate = 115200, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1, .source_clk = UART_SCLK_DEFAULT };
    uart_driver_install(UART_NUM_1, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TX_PIN, RX_PIN, -1, -1);

    uint8_t *data = (uint8_t *) malloc(BUF_SIZE + 1);
    ESP_LOGI(TAG, "System Ready. Waiting for STM32 TILT_ALERT...");

    while(1) {
        int len = uart_read_bytes(UART_NUM_1, data, BUF_SIZE, 20 / portTICK_PERIOD_MS);
        if (len > 0) {
            data[len] = '\0';
            if (strstr((char*)data, "TILT_ALERT")) {
                ESP_LOGW(TAG, "Motion Detected! Action: Capture and Send");
                capture_and_send();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}