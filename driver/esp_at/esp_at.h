#ifndef __ESP_AT_H__
#define __ESP_AT_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
	char ssid[64];  // WIFI名称
	bool connected; // 是否连接
	int8_t rssi;    // 信号强度。未连接或查询失败时保留上一次
	uint8_t channel; // 信道。未连接或查询失败时保留上一次
} esp_wifi_info_t;  // WIFI信息

typedef struct
{
    uint16_t year; // 年
    uint8_t month; // 月
    uint8_t day; // 日
    uint8_t hour; // 时
    uint8_t minute; // 分
    uint8_t second; // 秒
    uint8_t weekday; // 星期
} esp_date_time_t;  // 网络时间

bool esp_at_init(void);  // 初始化ESP AT
bool esp_at_wifi_init(void);  // 初始化WIFI
bool esp_at_connect_wifi(const char *ssid, const char *pwd, const char *mac);  // 连接WIFI
bool esp_at_get_wifi_info(esp_wifi_info_t *info);  // 获取WIFI信息
bool esp_at_get_ap_info(int8_t *rssi, uint8_t *channel);  // 已连接时查询信道和RSSI。失败不表示断开
bool esp_at_sntp_init(void);  // 初始化SNTP
bool esp_at_sntp_get_time(esp_date_time_t *date);  // 获取网络时间
const char *esp_at_http_get(const char *url);  // 发送HTTP GET请求
bool esp_at_tcp_listen(uint16_t port);  // 建立TCP服务器
bool esp_at_tcp_get_ip(char *ip, size_t size);  // 读取IP地址
bool esp_at_tcp_send(const uint8_t *data, uint16_t len);  // TCP定长发送数据
void esp_at_tcp_link_snapshot(int8_t *link_id, uint32_t *generation);  // 读取TCP链路ID和代次

#endif  //  __ESP_AT_H__
