/* My WiFi station Example

    https://github.com/espressif/esp-idf/blob/master/examples/wifi/getting_started/station/main/station_example_main.c
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
// #include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "wifi.h"

#define WIFI_STA_MAXIMUM_RETRY  CONFIG_WIFI_STA_MAXIMUM_RETRY

/* FreeRTOS event group to signal when we are connected */
static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static esp_err_t s_tcpip_started = ESP_FAIL;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi";

static uint8_t s_retry_num = 0;
static uint8_t s_max_retry = WIFI_STA_MAXIMUM_RETRY;
static uint8_t s_wifi_status = WIFI_STATUS_OFF; 

static esp_event_handler_instance_t s_instance_any_id;
static esp_event_handler_instance_t s_instance_got_ip;


// dummy wi-fi status
uint8_t wifi_status_get(void)
{
	//int64_t tsf_time = esp_wifi_get_tsf_time(WIFI_IF_STA);
	//ESP_LOGI(TAG, "tsf_time=%lli", tsf_time);
	return s_wifi_status;
}

void wifi_max_retry_set(uint8_t max_retry)
{
    s_max_retry = max_retry;
}



static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < s_max_retry) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry %d to connect to the AP", s_retry_num);
        } else {
            s_retry_num = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG,"connect to the AP fail");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}


esp_err_t wifi_sta_start(const char* wifi_sta_ssid, const char* wifi_sta_pass, const esp_netif_ip_info_t *ip_info)
{
    //Initialize Non-volatile storage
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    s_wifi_event_group = xEventGroupCreate();
    
    
    // Initialize the underlying TCP/IP stack.
    // This function should be called exactly once from application code, when the application starts up.
    if (s_tcpip_started == ESP_FAIL) {
        s_tcpip_started = esp_netif_init();
        ESP_ERROR_CHECK(s_tcpip_started);
    }
    
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    s_sta_netif = esp_netif_create_default_wifi_sta();
    
    if (ip_info) { // set static IP address
        esp_netif_dhcpc_stop(s_sta_netif);
        esp_netif_set_ip_info(s_sta_netif, ip_info);
    }
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &s_instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &s_instance_got_ip));    
    
    wifi_config_t wifi_config = {};
    
    strncpy((char*)wifi_config.sta.ssid, wifi_sta_ssid, 32);
    strncpy((char*)wifi_config.sta.password, wifi_sta_pass, 64);

    /* Setting a password implies station will connect to all security modes including WEP/WPA.
     * However these modes are deprecated and not advisable to be used. Incase your Access point
     * doesn't support WPA2, these mode can be enabled by commenting below line */
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true; 
    wifi_config.sta.pmf_cfg.required = false;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );
    ESP_LOGI(TAG, "wifi_init_sta finished.");
    
    s_wifi_status = WIFI_STATUS_OFF; 
    
    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", 
                (char*)wifi_config.sta.ssid, (char*)wifi_config.sta.password);
        s_wifi_status = WIFI_STATUS_CONNECTED;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", 
                (char*)wifi_config.sta.ssid, (char*)wifi_config.sta.password);
        ret = ESP_FAIL;
        s_wifi_status = WIFI_STATUS_FAIL;
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        ret = ESP_FAIL;
        s_wifi_status = WIFI_STATUS_FAIL;
    }
    
    
    return ret;
}


// https://github.com/espressif/esp-idf/blob/master/examples/common_components/protocol_examples_common/connect.c

void wifi_sta_stop(void)
{
    /* The event will not be processed after unregister */
        
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_instance_any_id));

	s_wifi_status = WIFI_STATUS_OFF;
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        return;
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_wifi_deinit());

    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(s_sta_netif));
    esp_netif_destroy(s_sta_netif);

    ESP_ERROR_CHECK(esp_event_loop_delete_default());
    
    vEventGroupDelete(s_wifi_event_group);

}

// --- SoftAP ---

// https://github.com/espressif/esp-idf/tree/v4.4/examples/wifi/getting_started/softAP

/* The examples use WiFi configuration that you can set via project configuration menu.
   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/

#define WIFI_AP_CHANNEL   CONFIG_WIFI_AP_CHANNEL
#define WIFI_AP_MAX_STA_CONN  CONFIG_WIFI_AP_MAX_STA_CONN 


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

// https://github.com/espressif/esp-idf/issues/8698
esp_err_t wifi_ap_start(const char* wifi_ap_ssid, const char* wifi_ap_pass, const esp_netif_ip_info_t *ip_info)
{
    //Initialize Non-volatile storage
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    //esp_netif_create_default_wifi_ap();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (ip_info) { // set static IP address
        ESP_ERROR_CHECK(esp_netif_dhcps_stop(s_ap_netif));
        ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, ip_info));
        ESP_ERROR_CHECK(esp_netif_dhcps_start(s_ap_netif));
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    
    wifi_config_t wifi_config = {};
    
    strncpy((char*)wifi_config.ap.ssid, wifi_ap_ssid, 32);
    strncpy((char*)wifi_config.ap.password, wifi_ap_pass, 64);

    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.ap.channel = WIFI_AP_CHANNEL;
    wifi_config.ap.max_connection = WIFI_AP_MAX_STA_CONN;
    
    
    if (strlen((char*)wifi_config.ap.password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             (char*)wifi_config.ap.ssid, (char*)wifi_config.ap.password, WIFI_AP_CHANNEL);
    
    ESP_LOGI(TAG, "WIFI_MODE_AP");
    return ret;
}

void wifi_reconnect(void)
{
    s_retry_num = 0;
    esp_wifi_connect();
}

void wifi_ap_stop(void)
{
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        return;
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(s_ap_netif));
    esp_netif_destroy(s_ap_netif);

    ESP_ERROR_CHECK(esp_event_loop_delete_default());
}
