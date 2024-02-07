#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "esp_system.h"
#include "esp_log.h"
#include "ping.h"
#include "wifi.h"
#include "esp_heap_caps.h"

static const char TAG[] = "test_wifi";

static void test_teardown(void)
{
    TEST_ASSERT_TRUE(heap_caps_check_integrity_all(true));
    ESP_LOGI(TAG, "free heap size: %d", esp_get_free_heap_size());
}

// STA with leaking memory
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
    test_teardown();
}

// AP without leaking memory!
TEST_CASE("access point", "[wifi]")
{    
    TEST_ASSERT_EQUAL(ESP_OK, wifi_ap_start(WIFI_AP_SSID, WIFI_AP_PASS, NULL));
    vTaskDelay(pdMS_TO_TICKS(200)); 
    wifi_ap_stop();
    test_teardown();
}


TEST_CASE("ping", "[wifi]")
{

    esp_netif_ip_info_t *p_ip_info = NULL; // use DHCP default
    TEST_ASSERT_EQUAL(ESP_OK, wifi_sta_start(WIFI_STA_SSID, WIFI_STA_PASS, p_ip_info, 0, 0));
    
    TEST_ESP_OK(ping_initialize(1000, 2, "www.espressif.com")); //
        
    wifi_sta_stop();
    test_teardown();
}


TEST_CASE("sntp", "[wifi]")
{
    TEST_ESP_OK(wifi_sta_start(WIFI_STA_SSID, WIFI_STA_PASS, NULL, 0, 0));
    
    bool res = wifi_sta_sntp_init("pool.ntp.org"); // ntp1.stratum2.ru
    wifi_sta_stop(); 
    
    TEST_ASSERT_TRUE(res);
    test_teardown();
}
