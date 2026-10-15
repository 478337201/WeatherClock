#include "main_page.h"
#include "wifi.h"
#include "ui.h"
#include "image.h"
#include "tcp.h"

#include <stdio.h>

/**
 * @brief 格式化温度
 * @param str 温度字符串
 * @param size 温度字符串大小
 * @param temperature 温度
 */
static void format_temperature(char *str, size_t size, float temperature)
{
    if (temperature > -10.0f && temperature <= 100.0f)
        snprintf(str, size, "%.0f", temperature);
    else
        snprintf(str, size, "--");
}

/**
 * @brief 显示主页面
 */
void main_page_display(void)
{
    ui_send_text(UI_ACTION_SHOW_MAIN, NULL);
    main_page_redraw_wifi_ssid(WIFI_SSID);
    main_page_redraw_inner_temperature(999.9f);
    main_page_redraw_inner_humidity(999.9f);
    main_page_redraw_outdoor_city("\xE8\xA5\xBF\xE5\xAE\x89");  // 西安
    main_page_redraw_outdoor_temperature(999.9f);
    main_page_redraw_outdoor_weather_icon(-1);
    main_page_show_ip(tcp_ip());
    tcp_cache_page(TCP_PAGE_MAIN, "");
}

/**
 * @brief 显示IPv4
 * @param ip 点分地址。空指针或空串显示--
 */
void main_page_show_ip(const char *ip)
{
    if (ip == NULL || ip[0] == '\0')
        ip = "--";
    ui_send_text(UI_ACTION_SET_IP, ip);
}

/**
 * @brief 更新WIFI SSID UI
 * @param ssid WIFI SSID
 */
void main_page_redraw_wifi_ssid(const char *ssid)
{
    ui_send_text(UI_ACTION_SET_SSID, ssid);
}

/**
 * @brief 更新时间UI
 * @param time 时间
 */
void main_page_redraw_time(rtc_date_time_t *time)
{
    ui_set_time(time->hour, time->minute, time->second);
}

/**
 * @brief 更新日期UI
 * @param date 日期
 */
void main_page_redraw_date(rtc_date_time_t *date)
{
    const char *weekday;
    char str[32];

    switch (date->weekday)
    {
        case 1: weekday = "\xE4\xB8\x80"; break; /* 一 */
        case 2: weekday = "\xE4\xBA\x8C"; break; /* 二 */
        case 3: weekday = "\xE4\xB8\x89"; break; /* 三 */
        case 4: weekday = "\xE5\x9B\x9B"; break; /* 四 */
        case 5: weekday = "\xE4\xBA\x94"; break; /* 五 */
        case 6: weekday = "\xE5\x85\xAD"; break; /* 六 */
        case 7: weekday = "\xE6\x97\xA5"; break; /* 日 */
        default: weekday = "X"; break;
    }

    snprintf(str, sizeof(str), "%04u/%02u/%02u \xE6\x98\x9F\xE6\x9C\x9F%s", /* 星期 */
        date->year, date->month, date->day, weekday);
    ui_send_text(UI_ACTION_SET_DATE, str);
}

/**
 * @brief 更新内部温度UI
 * @param temperature 内部温度
 */
void main_page_redraw_inner_temperature(float temperature)
{
    char str[8];
    format_temperature(str, sizeof(str), temperature);
    ui_send_text(UI_ACTION_SET_INNER_TEMP, str);
}

/**
 * @brief 更新内部湿度UI
 * @param humidity 内部湿度
 */
void main_page_redraw_inner_humidity(float humidity)
{
    char str[8];
    if (humidity > 0.0f && humidity <= 99.99f)
        snprintf(str, sizeof(str), "%.0f", humidity);
    else
        snprintf(str, sizeof(str), "--");
    ui_send_text(UI_ACTION_SET_INNER_HUMIDITY, str);
}

/**
 * @brief 更新外部城市UI
 * @param city 外部城市
 */
void main_page_redraw_outdoor_city(const char *city)
{
    ui_send_text(UI_ACTION_SET_OUTDOOR_CITY, city);
}

/**
 * @brief 更新外部温度UI
 * @param temperature 外部温度
 */
void main_page_redraw_outdoor_temperature(float temperature)
{
    char str[8];
    format_temperature(str, sizeof(str), temperature);
    ui_send_text(UI_ACTION_SET_OUTDOOR_TEMP, str);
}

/**
 * @brief 更新外部天气图标UI
 * @param code 外部天气图标代码
 */
void main_page_redraw_outdoor_weather_icon(const int code)
{
    const lv_img_dsc_t *icon;
    if (code == 0 || code == 2 || code == 38)
        icon = &icon_qing;
    else if (code == 1 || code == 3)
        icon = &icon_yueliang;
    else if (code == 4 || code == 9)
        icon = &icon_yintian;
    else if (code == 5 || code == 6 || code == 7 || code == 8)
        icon = &icon_duoyun;
    else if (code == 10 || code == 13 || code == 14 || code == 15 || code == 16 || code == 17 || code == 18 || code == 19)
        icon = &icon_zhongyu;
    else if (code == 11 || code == 12)
        icon = &icon_leizhenyu;
    else if (code == 20 || code == 21 || code == 22 || code == 23 || code == 24 || code == 25)
        icon = &icon_zhongxue;
    else
        icon = &icon_na;
    ui_set_outdoor_icon(icon);
}
