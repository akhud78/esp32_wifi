#ifndef WIFI_H
#define WIFI_H

#include "sdkconfig.h"

/* The examples use WiFi configuration that you can set via project configuration menu
   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/

// Station
#define WIFI_STA_SSID       CONFIG_WIFI_STA_SSID
#define WIFI_STA_PASS       CONFIG_WIFI_STA_PASSWORD
// Access Point
#define WIFI_AP_SSID        CONFIG_WIFI_AP_SSID
#define WIFI_AP_PASS        CONFIG_WIFI_AP_PASSWORD
// Static IP
#ifdef CONFIG_WIFI_STATIC
#define WIFI_STATIC_IP      CONFIG_WIFI_STATIC_IP
#define WIFI_STATIC_GW      CONFIG_WIFI_STATIC_GW
#define WIFI_STATIC_MASK    CONFIG_WIFI_STATIC_MASK
#endif


#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_sta_start(const char* wifi_sta_ssid, const char* wifi_sta_pass, const esp_netif_ip_info_t* ip_info, 
                        uint8_t max_retry, uint16_t time_retry);
void wifi_sta_stop(void);
bool wifi_sta_sntp_init(const char *server);

esp_err_t wifi_ap_start(const char* wifi_ap_ssid, const char* wifi_ap_pass, const esp_netif_ip_info_t *ip_info);
void wifi_ap_stop(void);


#define WIFI_STATUS_OFF       (0)
#define WIFI_STATUS_CONNECTED (1) 
#define WIFI_STATUS_FAIL      (2)        

uint8_t wifi_status_get(void);



#ifdef __cplusplus
}
#endif


#endif // WIFI_H 
