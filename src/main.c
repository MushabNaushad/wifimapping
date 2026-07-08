#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

#define CSI_MAX_LEN 400

#define WIFI_SSID "Dialog 4G 236"
#define WIFI_PASS "pwd123" 

typedef struct {
    int8_t rssi;
    uint16_t len;
    int8_t buf[CSI_MAX_LEN];
} csi_data_t;

static QueueHandle_t csi_queue;

void csi_callback(void *ctx, wifi_csi_info_t *info) {
    if (!info || !info->buf) return; 
    
    csi_data_t item;
    item.rssi = info->rx_ctrl.rssi;
    item.len  = info->len > CSI_MAX_LEN ? CSI_MAX_LEN : info->len;
    memcpy(item.buf, info->buf, item.len);

    xQueueSend(csi_queue, &item, 0);
}

void csi_process_task(void *arg){
    csi_data_t item;
    for(;;){
        if (xQueueReceive(csi_queue, &item, portMAX_DELAY) == pdTRUE){
            printf("CSI_DATA,%d,", item.rssi);
            for(uint16_t i = 0; i < item.len; i++){
                printf("%d,", item.buf[i]);
            }
            printf("\n");
            
            fflush(stdout);
            vTaskDelay(100 / portTICK_PERIOD_MS); 
        }
    }
}


static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        printf("Reconnecting to router...\n");
    } else if (event_id == IP_EVENT_STA_GOT_IP) {
        printf("Connected! IP Assigned. CSI should start flooding now.\n");
    }
}

void wifi_init_active_csi(void){
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_csi_config_t csi_config = {
        .lltf_en = true,
        .htltf_en = true,
        .stbc_htltf2_en = false,
        .ltf_merge_en = true,
        .channel_filter_en = true,
        .manu_scale = false,
        .shift = false
    };

    csi_queue = xQueueCreate(10, sizeof(csi_data_t));
    xTaskCreate(csi_process_task, "csi_process", 4096, NULL, 5, NULL);

    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(&csi_callback, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
    
}

void app_main(void){
    wifi_init_active_csi();
    
    while(1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}