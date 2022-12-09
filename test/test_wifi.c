#include "unity.h"
#include "esp_log.h"
#include "ping.h"
#include "wifi.h"


TEST_CASE("station", "[wifi]")
{    

    esp_netif_ip_info_t *p_ip_info = NULL; // use DHCP default
#ifdef CONFIG_WIFI_STATIC
    esp_netif_ip_info_t ip_info; // for static IP
    ip_info.ip.addr = ipaddr_addr(WIFI_STATIC_IP);
    ip_info.gw.addr = ipaddr_addr(WIFI_STATIC_GW);
    ip_info.netmask.addr = ipaddr_addr(WIFI_STATIC_MASK);
    p_ip_info = &ip_info;
#endif

    TEST_ASSERT_EQUAL(ESP_OK, wifi_sta_start(WIFI_STA_SSID, WIFI_STA_PASS, p_ip_info, 0, 0));
    wifi_sta_stop();    
}


TEST_CASE("access point", "[wifi]")
{    
    TEST_ASSERT_EQUAL(ESP_OK, wifi_ap_start(WIFI_AP_SSID, WIFI_AP_PASS, NULL));
    wifi_ap_stop();
}


TEST_CASE("ping", "[wifi]")
{

    esp_netif_ip_info_t *p_ip_info = NULL; // use DHCP default
    TEST_ASSERT_EQUAL(ESP_OK, wifi_sta_start(WIFI_STA_SSID, WIFI_STA_PASS, p_ip_info, 0, 0));
    
    TEST_ESP_OK(ping_initialize(1000, 2, "www.espressif.com")); //
        
    wifi_sta_stop();
    
}
