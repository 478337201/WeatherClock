#ifndef __TCP_H__
#define __TCP_H__

#include "rtc.h"

#include <stdbool.h>
#include <stdint.h>

#define TCP_PAGE_MAIN    0  // 主页
#define TCP_PAGE_ERROR   1  // 错误页

void tcp_init(void);  // 开启TCP监听
const char *tcp_ip(void);  // 获取IP地址
void tcp_on_second(void);  // 请求TCP发送心跳数据到客户端（说明TCP连接正常）
void tcp_cache_time(const rtc_date_time_t *time);  // 请求TCP发送时间数据到客户端
void tcp_cache_indoor(float temperature, float humidity);  // 请求TCP发送室内数据到客户端
void tcp_cache_outdoor(const char *city, const char *text, int code, float temperature);  // 请求TCP发送室外数据到客户端
void tcp_cache_wifi(bool connected, const char *ssid, int8_t rssi, uint8_t channel);  // 请求TCP发送WiFi数据到客户端
void tcp_cache_page(uint8_t page, const char *text);  // 请求TCP发送页面数据到客户端

#endif  // __TCP_H__
