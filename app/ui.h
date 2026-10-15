#ifndef __APP_UI_H__
#define __APP_UI_H__

#include <stdint.h>
#include "lvgl.h"

typedef enum
{
    UI_ACTION_SHOW_WELCOME,
    UI_ACTION_SHOW_WIFI,
    UI_ACTION_SHOW_ERROR,
    UI_ACTION_SHOW_MAIN,
    UI_ACTION_SET_TIME,
    UI_ACTION_SET_DATE,
    UI_ACTION_SET_IP,
    UI_ACTION_SET_SSID,
    UI_ACTION_SET_INNER_TEMP,
    UI_ACTION_SET_INNER_HUMIDITY,
    UI_ACTION_SET_OUTDOOR_CITY,
    UI_ACTION_SET_OUTDOOR_TEMP,
    UI_ACTION_SET_OUTDOOR_ICON,
} ui_action_t;  // UI动作枚举

void ui_init(void);  // 初始化UI队列
void ui_send_text(ui_action_t action, const char *text);  // 发送文本消息
void ui_show_welcome(void);  // 显示欢迎页面
void ui_show_wifi(const char *ssid);  // 显示WiFi连接页面
void ui_set_time(uint8_t hour, uint8_t minute, uint8_t second);  // 设置时间
void ui_set_outdoor_icon(const lv_img_dsc_t *icon);  // 设置外部图标

#endif  // __APP_UI_H__
