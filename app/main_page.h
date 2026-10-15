#ifndef __MAIN_PAGE_H__
#define __MAIN_PAGE_H__

#include "rtc.h"

void main_page_display(void);  // 显示主页面
void main_page_show_ip(const char *ip);  // 显示IPv4，空串显示--
void main_page_redraw_wifi_ssid(const char *ssid);  // 更新WIFI SSID UI
void main_page_redraw_time(rtc_date_time_t *time);  // 更新时间UI
void main_page_redraw_date(rtc_date_time_t *date);  // 更新日期UI
void main_page_redraw_inner_temperature(float temperature);  // 更新内部温度UI
void main_page_redraw_inner_humidity(float humidity);  // 更新内部湿度UI
void main_page_redraw_outdoor_city(const char *city);  // 更新外部城市UI
void main_page_redraw_outdoor_temperature(float temperature);  // 更新外部温度UI
void main_page_redraw_outdoor_weather_icon(const int code);  // 更新外部天气图标UI

#endif // __MAIN_PAGE_H__
