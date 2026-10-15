#include "wifi.h"
#include "FreeRTOS.h"
#include "task.h"
#include "esp_at.h"
#include "ui.h"
#include "elog.h"
#include "board.h"

#include <stdint.h>
#include <stdio.h>


/**
 * @brief 初始化WiFi
 */
void wifi_init(void)
{
    if (!esp_at_init())   // 初始化AT
    {
#if IS_DEBUG_VERSION
        elog_e("AT", "init failed\r\n");
#endif
        goto err;
    }
#if IS_DEBUG_VERSION
    elog_i("AT", "inited\r\n");
#endif
    if (!esp_at_wifi_init())  // 初始化WiFi
    {
#if IS_DEBUG_VERSION
        elog_e("WIFI", "init failed\r\n");
#endif
        goto err;
    }
#if IS_DEBUG_VERSION
    elog_i("WIFI", "inited\r\n");
#endif
    
    if (!esp_at_sntp_init())  // 初始化SNTP
    {
#if IS_DEBUG_VERSION
        elog_e("SNTP", "init failed\r\n");
#endif
        goto err;
    }
#if IS_DEBUG_VERSION
    elog_i("SNTP", "inited\r\n");
#endif
	
    return;  // 初始化戝功
    
err:
    ui_send_text(UI_ACTION_SHOW_ERROR, "wireless init failed");  // 显示WiFi初始化失败错误
    while (1) {}  // 代砝丝应该违行到这里
}

/**
 * @brief 等待WiFi连接
 */
void wifi_wait_connect(void)
{
#if IS_DEBUG_VERSION
    elog_i("WIFI", "connecting\r\n");
#endif
	
    esp_at_connect_wifi(WIFI_SSID, WIFI_PASSWD, NULL);  // 连接WiFi
    
    for (uint32_t t = 0; t < 100; t++)  // 等待10秒
    {
        vTaskDelay(pdMS_TO_TICKS(100));  // 等待100毫秒
        esp_wifi_info_t wifi = { 0 };
        if (esp_at_get_wifi_info(&wifi) && wifi.connected)  // 获坖WiFi连接信杯
        {
#if IS_DEBUG_VERSION
            elog_i("WIFI", "Connected to %s\r\n", wifi.ssid);
#endif
            return;
        }
    }
    
#if ELOG_TRIG
    elog_e("WIFI", "Connection Timeout\r\n");
#endif

    ui_send_text(UI_ACTION_SHOW_ERROR, "wireless connect failed");  // 显示WiFi连接失败错误
    while (1) {}  // 代砝丝应该违行到这里
}
