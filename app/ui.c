#include "ui.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "st7789.h"
#include "image.h"
#include "wifi.h"
#include "elog.h"
#include "board.h"
#include "font_maple.h"

#include <string.h>

#define DRAW_BUF_LINES 20  // 绘制缓冲区行数
#define UI_TEXT_SIZE   48  // 文本大小

typedef struct
{
    ui_action_t action;  // UI动作
    union
    {
        struct
        {
            uint8_t hour;  // 小时
            uint8_t minute;  // 分钟
            uint8_t second;  // 秒
        } time;  // 时间结构体
        char text[UI_TEXT_SIZE];  // 文本
        const lv_img_dsc_t *icon;  // 图标
    };  // 联合体
} ui_message_t;  // UI消息结构体

static QueueHandle_t ui_queue = NULL;  // UI队列
static lv_disp_draw_buf_t draw_buf = {0};  // 显示缓冲区
static lv_color_t draw_buf_mem[ST7789_WIDTH * DRAW_BUF_LINES] = {0};  // 显示缓冲区内存
static lv_obj_t *ui_time_label = NULL;  // 时间标签
static lv_obj_t *ui_date_label = NULL;  // 日期标签
static lv_obj_t *ui_ip_label = NULL;  // IP标签
static lv_obj_t *ui_ssid_label = NULL;  // SSID标签
static lv_obj_t *ui_inner_temp_label = NULL;  // 内部温度标签
static lv_obj_t *ui_inner_humidity_label = NULL;  // 内部湿度标签
static lv_obj_t *ui_outdoor_city_label = NULL;  // 外部城市标签
static lv_obj_t *ui_outdoor_temp_label = NULL;  // 外部温度标签
static lv_obj_t *ui_outdoor_icon = NULL;  // 外部图标

/**
* @brief LVGL刷新回调函数
* @param drv 显示驱动
* @param area 区域
* @param color_p 颜色指针
*/
static void lvgl_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t pixels = (uint32_t)(area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    st7789_write_pixels(area->x1, area->y1, area->x2, area->y2,
        (const uint8_t *)color_p, pixels * 2);
    lv_disp_flush_ready(drv);
}

/**
* @brief 创建屏幕
* @return 屏幕对象
*/
static lv_obj_t *screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

/**
* @brief 创建卡片
* @param parent 父对象
* @param x x坐标
* @param y y坐标
* @param w 宽度
* @param h 高度
* @param color 颜色
* @return 卡片对象
*/
static lv_obj_t *card_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
    lv_coord_t w, lv_coord_t h, lv_color_t color)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, color, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

/**
* @brief 创建标签
* @param parent 父对象
* @param x x坐标
* @param y y坐标
* @param color 颜色
* @param text 文本
* @param font 字体
* @return 标签对象
*/
static lv_obj_t *label_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
    lv_color_t color, const char *text, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

/**
* @brief 创建图片
* @param parent 父对象
* @param x x坐标
* @param y y坐标
* @param src 图片源
* @return 图片对象
*/
static lv_obj_t *image_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, const lv_img_dsc_t *src)
{
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, src);
    lv_obj_set_pos(img, x, y);
    return img;
}

/**
* @brief 清除主界面组件
*/
static void clear_main_widgets(void)
{
    ui_time_label = NULL;
    ui_date_label = NULL;
    ui_ip_label = NULL;
    ui_ssid_label = NULL;
    ui_inner_temp_label = NULL;
    ui_inner_humidity_label = NULL;
    ui_outdoor_city_label = NULL;
    ui_outdoor_temp_label = NULL;
    ui_outdoor_icon = NULL;
}

/**
* @brief UI渲染任务
* @param param 任务参数
*/
static void ui_func(void *param)
{
    st7789_init();  // 初始化ST7789显示器
    lv_init();  // 初始化LVGL
    lv_disp_draw_buf_init(&draw_buf, draw_buf_mem, NULL, ST7789_WIDTH * DRAW_BUF_LINES);  // 初始化显示缓冲区
    
    static lv_disp_drv_t disp_drv; // 显示驱动
    lv_disp_drv_init(&disp_drv); // 初始化显示驱动
    disp_drv.hor_res = ST7789_WIDTH; // 水平分辨率
    disp_drv.ver_res = ST7789_HEIGHT; // 垂直分辨率
    disp_drv.flush_cb = lvgl_flush; // 刷新回调函数
    disp_drv.draw_buf = &draw_buf; // 显示缓冲区
    lv_disp_drv_register(&disp_drv); // 注册显示驱动

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0); // 设置背景颜色
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0); // 设置背景透明度

    ui_message_t msg;
    while (1)  // 循环接收UI消息
    {
        if (xQueueReceive(ui_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE)  // 接收UI消息
        {
            do  // 处理UI消息
            {
                switch (msg.action)  // 处理UI动作
                {
                    case UI_ACTION_SHOW_WELCOME:
                    {
                        lv_obj_t *scr = screen_create();
                        clear_main_widgets();
                        image_create(scr, 30, 10, &img_meihua);
                        label_create(scr, 40, 205, lv_color_make(237, 128, 147), "\xE6\xA2\x85\xE8\x8A\xB1\xE5\xB5\x8C\xE5\x85\xA5\xE5\xBC\x8F", &font_maple_32_bold); /* 梅花嵌入式 */
                        label_create(scr, 56, 233, lv_color_make(86, 165, 255), "\xE5\xA4\xA9\xE6\xB0\x94\xE6\x97\xB6\xE9\x92\x9F", &font_maple_32_bold); /* 天气时钟 */
                        label_create(scr, 60, 285, lv_color_white(), "loading...", &font_maple_24_bold);
                        lv_scr_load(scr);
                        break;
                    }
                    case UI_ACTION_SHOW_WIFI:
                    {
                        lv_obj_t *scr = screen_create();
                        lv_obj_t *ssid_label;
                        clear_main_widgets();
                        image_create(scr, 30, 15, &img_wifi);
                        label_create(scr, 88, 191, lv_color_make(0, 255, 234), "WiFi", &font_maple_32_bold);
                        ssid_label = label_create(scr, 0, 231, lv_color_white(), msg.text, &font_maple_20_bold);
                        lv_obj_align(ssid_label, LV_ALIGN_TOP_MID, 0, 231);
                        label_create(scr, 84, 263, lv_color_make(148, 198, 255), "\xE8\xBF\x9E\xE6\x8E\xA5\xE4\xB8\xAD", &font_maple_24_bold); /* 连接中 */
                        lv_scr_load(scr);
                        break;
                    }
                    case UI_ACTION_SHOW_ERROR:
                    {
                        lv_obj_t *scr = screen_create();
                        lv_obj_t *msg_label;
                        clear_main_widgets();
                        image_create(scr, 40, 37, &img_error);
                        msg_label = label_create(scr, 0, 245, lv_color_make(255, 255, 0), msg.text, &font_maple_20_bold);
                        lv_obj_align(msg_label, LV_ALIGN_TOP_MID, 0, 245);
                        lv_scr_load(scr);
                        break;
                    }
                    case UI_ACTION_SHOW_MAIN:
                    {
                        lv_obj_t *scr = screen_create();
                        lv_color_t gray = lv_color_make(143, 143, 143);
                        lv_color_t black = lv_color_black();

                        clear_main_widgets();
                        card_create(scr, 15, 15, 210, 140, lv_color_make(248, 248, 248));
                        card_create(scr, 15, 165, 100, 140, lv_color_make(136, 217, 234));
                        card_create(scr, 125, 165, 100, 140, lv_color_make(254, 135, 75));

                        image_create(scr, 23, 20, &icon_wifi);
                        ui_ssid_label = label_create(scr, 50, 23, gray, WIFI_SSID, &font_maple_16);
                        ui_time_label = label_create(scr, 25, 42, black, "--:--", &font_maple_76_extrabold);
                        ui_date_label = label_create(scr, 35, 104, gray, "----/--/-- \xE6\x98\x9F\xE6\x9C\x9F\xE5\x9B\x9B", &font_maple_20_bold); /* 星期四 */
                        ui_ip_label = label_create(scr, 35, 128, gray, "--", &font_maple_16);

                        label_create(scr, 19, 170, black, "\xE5\xAE\xA4\xE5\x86\x85\xE7\x8E\xAF\xE5\xA2\x83", &font_maple_24_semibold); /* 室内环境 */
                        ui_inner_temp_label = label_create(scr, 30, 192, black, "--", &font_maple_54_semibold);
                        label_create(scr, 86, 191, black, "C", &font_maple_32_bold);
                        ui_inner_humidity_label = label_create(scr, 25, 239, black, "--", &font_maple_64_extrabold);
                        label_create(scr, 91, 262, black, "%", &font_maple_32_bold);

                        ui_outdoor_city_label = label_create(scr, 127, 170, black, "\xE8\xA5\xBF\xE5\xAE\x89", &font_maple_24_bold); /* 西安 */
                        ui_outdoor_temp_label = label_create(scr, 135, 190, black, "--", &font_maple_54_bold);
                        label_create(scr, 192, 189, black, "C", &font_maple_32_bold);
                        image_create(scr, 139, 239, &icon_wenduji);
                        ui_outdoor_icon = image_create(scr, 166, 240, &icon_na);
                        lv_scr_load(scr);
                        break;
                    }
                    case UI_ACTION_SET_TIME:
                        if (ui_time_label != NULL)
                        {
                            lv_label_set_text_fmt(ui_time_label, "%02u%c%02u",
                                (unsigned)msg.time.hour,
                                (msg.time.second % 2 == 0) ? ':' : ' ',
                                (unsigned)msg.time.minute);
                        }
                        break;
                    case UI_ACTION_SET_DATE:
                        if (ui_date_label != NULL)
                            lv_label_set_text(ui_date_label, msg.text != NULL ? msg.text : "");
                        break;
                    case UI_ACTION_SET_IP:
                        if (ui_ip_label != NULL)
                            lv_label_set_text(ui_ip_label, msg.text != NULL ? msg.text : "");
                        break;
                    case UI_ACTION_SET_SSID:
                        if (ui_ssid_label != NULL)
                            lv_label_set_text(ui_ssid_label, msg.text != NULL ? msg.text : "");
                        break;
                    case UI_ACTION_SET_INNER_TEMP:
                        if (ui_inner_temp_label != NULL)
                            lv_label_set_text(ui_inner_temp_label, msg.text != NULL ? msg.text : "");
                        break;
                    case UI_ACTION_SET_INNER_HUMIDITY:
                        if (ui_inner_humidity_label != NULL)
                            lv_label_set_text(ui_inner_humidity_label, msg.text != NULL ? msg.text : "");
                        break;
                    case UI_ACTION_SET_OUTDOOR_CITY:
                        if (ui_outdoor_city_label != NULL)
                            lv_label_set_text(ui_outdoor_city_label, msg.text != NULL ? msg.text : "");
                        break;
                    case UI_ACTION_SET_OUTDOOR_TEMP:
                        if (ui_outdoor_temp_label != NULL)
                            lv_label_set_text(ui_outdoor_temp_label, msg.text != NULL ? msg.text : "");
                        break;
                    case UI_ACTION_SET_OUTDOOR_ICON:
                        if (ui_outdoor_icon != NULL && msg.icon != NULL)
                            lv_img_set_src(ui_outdoor_icon, msg.icon);
                        break;
                    default:
                    #if IS_DEBUG_VERSION
                        elog_e("UI", "Unknown UI action: %d\n", msg.action);
                    #endif
                        break;
                }
            } while (xQueueReceive(ui_queue, &msg, 0) == pdTRUE);
        }

        lv_timer_handler();  // 处理LVGL定时器
    }
}

/**
* @brief 初始化UI队列
*/
void ui_init(void)
{
    ui_queue = xQueueCreate(16, sizeof(ui_message_t));
    configASSERT(ui_queue);
    xTaskCreate(ui_func, "ui", 4096, NULL, 8, NULL);  // 创建UI渲染任务
}

/**
* @brief 发送文本消息
* @param action UI动作
* @param text 文本
*/
void ui_send_text(ui_action_t action, const char *text)
{
    ui_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.action = action;
    if (text != NULL)
        strncpy(msg.text, text, sizeof(msg.text) - 1);
    xQueueSend(ui_queue, &msg, portMAX_DELAY);
}

/**
* @brief 显示欢迎页面
*/
void ui_show_welcome(void)
{
    ui_send_text(UI_ACTION_SHOW_WELCOME, NULL);
}

/**
* @brief 显示WiFi连接页面
*/
void ui_show_wifi(const char *ssid)
{
    ui_send_text(UI_ACTION_SHOW_WIFI, ssid);
}

/**
* @brief 设置时间
* @param hour 小时
* @param minute 分钟
* @param second 秒
*/
void ui_set_time(uint8_t hour, uint8_t minute, uint8_t second)
{
    ui_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.action = UI_ACTION_SET_TIME;
    msg.time.hour = hour;
    msg.time.minute = minute;
    msg.time.second = second;
    xQueueSend(ui_queue, &msg, portMAX_DELAY);
}

/**
* @brief 设置外部图标
* @param icon 图标
*/
void ui_set_outdoor_icon(const lv_img_dsc_t *icon)
{
    ui_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.action = UI_ACTION_SET_OUTDOOR_ICON;
    msg.icon = icon;
    xQueueSend(ui_queue, &msg, portMAX_DELAY);
}
