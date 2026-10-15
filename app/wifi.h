#ifndef __WIFI_H__
#define __WIFI_H__

/* 由编译器按环境变量注入，见 tools/inject_wifi_defines.ps1 */
#ifndef WIFI_SSID
#error "未定义 WIFI_SSID。请设置环境变量 WIFI_SSID 后重新打开 Keil 再编译"
#endif
#ifndef WIFI_PASSWD
#error "未定义 WIFI_PASSWD。请设置环境变量 WIFI_PASSWD 后重新打开 Keil 再编译"
#endif

void wifi_init(void);  // 初始化WiFi
void wifi_wait_connect(void);  // 等待WiFi连接成功

#endif // __WIFI_H__
