#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "ping/ping_sock.h"
#include "lwip/inet.h"

#define CSI_MAX_LEN 400
#define SERVER_ADDR "192.168.8.182"
#define PORT 5555

#define WIFI_SSID "Dialog 4G 236"
#define WIFI_PASS "A15F8bd1" 

typedef struct __attribute__((packed)){
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

void udp_tx_task(void *arg){
    int udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udp_socket < 0) {
        printf("Failed to create socket!\n");
        vTaskDelete(NULL); 
    }

    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PORT);
    serverAddress.sin_addr.s_addr = inet_addr(SERVER_ADDR);

    csi_data_t item;
    for(;;){
        if (xQueueReceive(csi_queue, &item, portMAX_DELAY) == pdTRUE){
            int packet_size = sizeof(int8_t) + sizeof(uint16_t) + item.len;
            int err = sendto(udp_socket, &item, packet_size, 0, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
            if (err < 0) {
                // Handle Error If needed 
            }
        }
    }
}

static void silent_ping_end(esp_ping_handle_t hndl, void *args) {
    esp_ping_delete_session(hndl);
}

void ping_router(void){
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ipaddr_aton(SERVER_ADDR, &ping_config.target_addr);
    
    ping_config.count = 0; 
    ping_config.interval_ms = 100; // packet rate control

    esp_ping_callbacks_t cbs = {
        .on_ping_success = NULL, 
        .on_ping_timeout = NULL, 
        .on_ping_end = silent_ping_end,
        .cb_args = NULL
    };

    esp_ping_handle_t ping;
    esp_ping_new_session(&ping_config, &cbs, &ping);
    esp_ping_start(ping);
}


static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        printf("Reconnecting to router...\n");
    } else if (event_id == IP_EVENT_STA_GOT_IP) {
        printf("Connected! IP Assigned. CSI should start flooding now.\n");
        ping_router();
    }
}

void wifi_init_active_csi(void){

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    ESP_ERROR_CHECK(ret); 
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
    xTaskCreate(udp_tx_task, "udp_tx", 4096, NULL, 5, NULL);

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