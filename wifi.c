// https://github.com/espressif/esp-idf/blob/master/examples/wifi/getting_started/station/main/station_example_main.c
#include <string.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include "esp_mac.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "wifi.h"

// https://github.com/nopnop2002/esp-idf-ftpServer/blob/main/main/main.c
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
#define sntp_setoperatingmode   esp_sntp_setoperatingmode
#define sntp_setservername      esp_sntp_setservername
#define sntp_init               esp_sntp_init
#define sntp_enabled            esp_sntp_enabled
#define sntp_stop               esp_sntp_stop
#endif

#define WIFI_STA_MAXIMUM_RETRY      CONFIG_WIFI_STA_MAXIMUM_RETRY
#define WIFI_STA_TIME_RETRY         CONFIG_WIFI_STA_TIME_RETRY

static const char *TAG = "wifi";

static TimerHandle_t s_reconnect_timer;

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

static uint8_t s_retry_num = 0;
static uint8_t s_max_retry = WIFI_STA_MAXIMUM_RETRY;
static uint8_t s_wifi_status = WIFI_STATUS_OFF; 

static esp_event_handler_instance_t s_instance_any_id;
static esp_event_handler_instance_t s_instance_got_ip;

// dummy wi-fi status
uint8_t wifi_status_get(void)
{
    return s_wifi_status;
}


static void reconnect_timer_callback(TimerHandle_t timer)
{    
    if (s_wifi_status == WIFI_STATUS_FAIL) {  // reconnect
        s_retry_num = 0;
        esp_wifi_connect();
    }
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_wifi_status = WIFI_STATUS_OFF;
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_status = WIFI_STATUS_OFF;
        if (s_retry_num < s_max_retry) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry %d to connect to the AP", s_retry_num);
        } else {
            s_wifi_status = WIFI_STATUS_FAIL;
            s_retry_num = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG,"connect to the AP fail");
            xTimerStart(s_reconnect_timer, 0);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_status = WIFI_STATUS_CONNECTED;
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/*
    max_retry - Максимальное количество попыток соединения с точкой доступа
    time_retry - Время ожидания перед повтором  попыток соединения с точкой доступа
*/
esp_err_t wifi_sta_start(const char* wifi_sta_ssid, const char* wifi_sta_pass, const esp_netif_ip_info_t *ip_info, 
                        uint8_t max_retry, uint16_t time_retry)
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
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi configuration failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "%s esp_wifi_start OK", __func__); // DEBUG!!!
    
    if (time_retry == 0)
        time_retry = WIFI_STA_TIME_RETRY;
        
    if (max_retry) 
        s_max_retry = max_retry;
    
    
    s_reconnect_timer = xTimerCreate ( "reconnect_timer", time_retry * 1000 / portTICK_PERIOD_MS,
                                pdFALSE,         // The timers will auto-reload themselves when they expire.
                                (void *)0,      // Assign each timer a unique id equal to its array index.
                                reconnect_timer_callback  // Each timer calls the same callback when it expires.
                            );
    if(s_reconnect_timer == NULL) {
        ESP_LOGI(TAG, "%s The timer was not created", __func__);
        return ESP_FAIL; 
    }
    
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
        
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", 
                (char*)wifi_config.sta.ssid, (char*)wifi_config.sta.password);
        ret = ESP_FAIL;
        
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        ret = ESP_FAIL;
    }
    return ret;
}


// https://github.com/espressif/esp-idf/blob/master/examples/common_components/protocol_examples_common/connect.c

void wifi_sta_stop(void)
{
    xTimerStop(s_reconnect_timer, 0);
    
    if (sntp_enabled()) {
        sntp_stop();    
    }
    
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
// --- SNTP ---
// https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32s3/api-reference/system/system_time.html#sntp-time-synchronization
// https://github.com/espressif/esp-idf/blob/release/v4.4/examples/protocols/sntp/main/sntp_example_main.c
// https://github.com/espressif/esp-lwip/blob/master/src/include/lwip/apps/sntp.h
bool wifi_sta_sntp_init(const char *server)
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, server); // "pool.ntp.org"
    
    /*
    sntp_setservername(1, "europe.pool.ntp.org"); 
    sntp_setservername(2, "uk.pool.ntp.org ");
    sntp_setservername(3, "us.pool.ntp.org");
    sntp_setservername(4, "time1.google.com");
    */
        
    if (sntp_enabled()) {
        ESP_LOGI(TAG, "SNTP already initialized.");
        
    } else {    
        sntp_init();

        // wait for time to be set
        int retry = 0;
        const int retry_count = 5;
        while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
            ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    }

    time_t now = 0;
    struct tm timeinfo = {0};
    
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2016 - 1900))
        return false;
        
    char s[64];
    strftime(s, sizeof(s), "%Y-%m-%d %X", &timeinfo); // "%c"        
    ESP_LOGI(TAG, "%s", s);
    
    return true;        
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




