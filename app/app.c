#include "app.h"
#include "FreeRTOS.h"
#include "timers.h"
#include "workqueue.h"
#include "rtc.h"
#include "aht20.h"
#include "esp_at.h"
#include "weather.h"
#include "main_page.h"
#include "tcp.h"
#include "elog.h"
#include "board.h"

#include <stdint.h>
#include <string.h>

#define SECONDS(x) ((x) * 1000)                           // 秒
#define MINUTES(x)      SECONDS((x) * 60)                 // 分
#define HOURS(x)        MINUTES((x) * 60)                 // 时

#define TIME_SYNC_INTERVAL          HOURS(1)              // 时间同步间隔1小时   
#define WIFI_UPDATE_INTERVAL        SECONDS(5)            // WIFI更新间隔5秒
#define TIME_UPDATE_INTERVAL        SECONDS(1)            // 时间更新间隔1秒
#define INNER_UPDATE_INTERVAL       SECONDS(3)            // 内部更新间隔3秒
#define OUTDOOR_UPDATE_INTERVAL     MINUTES(1)            // 外部更新间隔1分钟

static TimerHandle_t time_sync_timer; // 时间同步定时器 
static TimerHandle_t wifi_update_timer;  // WIFI更新定时器
static TimerHandle_t time_update_timer; // 时间更新定时器
static TimerHandle_t inner_update_timer; // 内部更新定时器
static TimerHandle_t outdoor_update_timer; // 外部更新定时器

/**
* @brief 时间更新定时器任务函数
*/
static void time_update(void)
{
    static rtc_date_time_t last_date = { 0 };  // 保存上一秒时间
    
    rtc_date_time_t date;
    rtc_get_time(&date); // 获取RTC时间
    
    if (date.year >= 2020 && memcmp(&date, &last_date, sizeof(rtc_date_time_t)) != 0)
    {
        memcpy(&last_date, &date, sizeof(rtc_date_time_t));  // 保存当前时间
        main_page_redraw_time(&date); // 更新时间UI
        main_page_redraw_date(&date); // 更新日期UI
        tcp_cache_time(&date);  // 请求TCP发送时间数据到客户端
    }
    tcp_on_second();  // 请求TCP发送心跳数据到客户端（说明TCP连接正常）
}

/**
 * @brief 时间同步定时器任务函数
 */
static void time_sync(void)
{
    uint32_t restart_sync_delay = TIME_SYNC_INTERVAL;  // 时间同步间隔1小时
    rtc_date_time_t rtc_date = { 0 };  // RTC时间

    esp_date_time_t esp_date = { 0 };  // 网络时间
    if (!esp_at_sntp_get_time(&esp_date))  // 获取网络时间
    {
#if IS_DEBUG_VERSION
        elog_e("SNTP", "get time failed\r\n");
#endif
        restart_sync_delay = SECONDS(1);  // 时间同步失败，1秒后重试
        goto err;
    }
    
    if (esp_date.year < 2000)  // 无效时间
    {
#if IS_DEBUG_VERSION
        elog_e("SNTP", "invalid date formate\r\n");
#endif
        restart_sync_delay = SECONDS(1);  // 时间同步失败，1秒后重试
        goto err;
    }
    
#if IS_DEBUG_VERSION
    elog_i("SNTP", "sync time: %04u-%02u-%02u %02u:%02u:%02u (%d)\r\n", esp_date.year, esp_date.month, esp_date.day, esp_date.hour, esp_date.minute, esp_date.second, esp_date.weekday);
#endif
    rtc_date.year = esp_date.year;
    rtc_date.month = esp_date.month;
    rtc_date.day = esp_date.day;
    rtc_date.hour = esp_date.hour;
    rtc_date.minute = esp_date.minute;
    rtc_date.second = esp_date.second;
    rtc_date.weekday = esp_date.weekday;
    rtc_set_time(&rtc_date);  // 同步RTC时间
    
err:
    xTimerChangePeriod(time_sync_timer, pdMS_TO_TICKS(restart_sync_delay), 0);
}

/**
 * @brief WIFI更新定时器任务函数
 */
static void wifi_update(void)
{
    static esp_wifi_info_t last_info = { 0 };  // 保存上一次WIFI信息

    esp_wifi_info_t info = { 0 };  // 当前WIFI信息
    bool state_changed;
    bool radio_changed;

    if (!esp_at_get_wifi_info(&info))  // 获取WIFI信息
    {
#if IS_DEBUG_VERSION
        elog_e("AT", "wifi info get failed\r\n");
#endif
        return;
    }

    info.rssi = last_info.rssi;
    info.channel = last_info.channel;
    if (info.connected)  // 只有已连接才查AP。失败则留下上一次的rssi和信道
    {
        int8_t rssi = info.rssi;
        uint8_t channel = info.channel;
        if (esp_at_get_ap_info(&rssi, &channel))
        {
            info.rssi = rssi;
            info.channel = channel;
        }
    }

    state_changed = last_info.connected != info.connected;
    radio_changed = info.connected && (last_info.rssi != info.rssi || last_info.channel != info.channel);

    if (state_changed)  // 重画SSID只看连接状态，RSSI抖动不刷屏幕
    {
        if (info.connected)
        {
#if IS_DEBUG_VERSION
            elog_i("WIFI", "connected to %s\r\n", info.ssid);
#endif
            main_page_redraw_wifi_ssid(info.ssid);
        }
        else
        {
#if IS_DEBUG_VERSION
            elog_e("WIFI", "disconnected from %s\r\n", last_info.ssid);
#endif
            main_page_redraw_wifi_ssid("wifi lost");
        }
    }

    if (state_changed || radio_changed)
        memcpy(&last_info, &info, sizeof(esp_wifi_info_t));  // 保存当前WIFI信息
    tcp_cache_wifi(info.connected, info.ssid, info.rssi, info.channel);  // 没变也更新缓存，未送达的连接会补发
}

/**
 * @brief 内部更新定时器任务函数
 */
static void inner_update(void)
{
    static float last_temperature, last_humidity;  // 保存上一次温度和湿度
    
    if (!aht20_start_measurement())  // 启动温度和湿度测量
    {
#if IS_DEBUG_VERSION
        elog_e("AHT20", "start measurement failed\r\n");
#endif
        return;
    }
    
    if (!aht20_wait_for_measurement())  // 等待温度和湿度测量完成
    {
#if IS_DEBUG_VERSION
        elog_e("AHT20", "wait for measurement failed\r\n");
#endif
        return;
    }
    
    float temperature = 0.0f, humidity = 0.0f;  // 温度和湿度
    
    if (!aht20_read_measurement(&temperature, &humidity))  // 读取温度和湿度
    {
#if IS_DEBUG_VERSION
        elog_e("AHT20", "read measurement failed\r\n");
#endif
        return;
    }
    
    if (temperature != last_temperature || humidity != last_humidity)  // 有变化才重画屏幕
    {
        last_temperature = temperature;  // 保存当前温度
        last_humidity = humidity;  // 保存当前湿度
#if IS_DEBUG_VERSION
        elog_i("AHT20", "Temperature: %.1f, Humidity: %.1f\r\n", temperature, humidity);
#endif
        main_page_redraw_inner_temperature(temperature);  // 更新内部温度UI
        main_page_redraw_inner_humidity(humidity);  // 更新内部湿度UI
    }
    tcp_cache_indoor(temperature, humidity);  // 没变也更新缓存，未送达的连接会补发
}

/**
 * @brief 外部更新定时器任务函数
 */
static void outdoor_update(void)
{
    static weather_info_t last_weather = { 0 };  // 保存上一次天气信息
    
    weather_info_t weather = { 0 };  // 当前天气信息
    const char *weather_url = "https://api.seniverse.com/v3/weather/now.json?key=S50wCY8E2pGQBV-1c&location=Xian&language=zh-Hans&unit=c";  // 天气API URL
    const char *weather_http_response = esp_at_http_get(weather_url);  // 获取天气API响应
    if (weather_http_response == NULL)  // 天气API响应为空
    {
#if IS_DEBUG_VERSION
        elog_e("WEATHER", "http error\r\n");
#endif
        return;
    }
    
    if (!parse_seniverse_response(weather_http_response, &weather))  // 解析天气API响应
    {
#if IS_DEBUG_VERSION
        elog_e("WEATHER", "parse failed\r\n");
#endif
        return;
    }
    
    if (memcmp(&last_weather, &weather, sizeof(weather_info_t)) != 0)  // 有变化才重画屏幕
    {
        memcpy(&last_weather, &weather, sizeof(weather_info_t));  // 保存当前天气信息
#if IS_DEBUG_VERSION
        elog_i("WEATHER", "%s, %.1f\r\n", weather.city, weather.temperature);
#endif
        if (weather.city[0] != '\0')  // 城市不为空
            main_page_redraw_outdoor_city(weather.city);  // 更新外部城市UI
        main_page_redraw_outdoor_temperature(weather.temperature);  // 更新外部温度UI
        main_page_redraw_outdoor_weather_icon(weather.weather_code);  // 更新外部天气图标UI
    }
    tcp_cache_outdoor(weather.city, weather.weather, weather.weather_code, weather.temperature);  // 没变也更新缓存，未送达的连接会补发
}

typedef void (*app_job_t)(void);  // 应用程序任务函数

/**
* @brief 时间更新定时器回调函数
*/
static void app_timer_cb(TimerHandle_t timer)
{
    app_job_t job = (app_job_t)pvTimerGetTimerID(timer);  // 获取时间更新任务
    job();  // 执行time_update()任务
}

/**
 * @brief 工作队列任务函数
 */
static void app_work(void *param)
{
    app_job_t job = (app_job_t)param;
    job();
}

/**
 * @brief 工作队列定时器回调函数
 */
static void work_timer_cb(TimerHandle_t timer)
{
    app_job_t job = (app_job_t)pvTimerGetTimerID(timer);
    workqueue_run(app_work, job);
}

/**
 * @brief 初始化应用程序
 */
void app_init(void)
{
    // 创建时间更新定时器，定时周期1s，循环触发，定时器任务函数，定时器回调中直接执行
    time_update_timer = xTimerCreate("time update", pdMS_TO_TICKS(TIME_UPDATE_INTERVAL), pdTRUE, time_update, app_timer_cb);
    // 创建时间同步定时器，定时周期200ms（随后为1h），单次触发，定时器任务函数，定时器回调中提交到工作队列执行
    time_sync_timer = xTimerCreate("time sync", pdMS_TO_TICKS(200), pdFALSE, time_sync, work_timer_cb);
    // 创建WIFI更新定时器，定时周期5s，循环触发，定时器任务函数，定时器回调中提交到工作队列执行
    wifi_update_timer = xTimerCreate("wifi update", pdMS_TO_TICKS(WIFI_UPDATE_INTERVAL), pdTRUE, wifi_update, work_timer_cb);
    // 创建内部更新定时器，定时周期3s，循环触发，定时器任务函数，定时器回调中提交到工作队列执行
    inner_update_timer = xTimerCreate("inner upadte", pdMS_TO_TICKS(INNER_UPDATE_INTERVAL), pdTRUE, inner_update, work_timer_cb);
    // 创建外部更新定时器，定时周期1分钟，循环触发，定时器任务函数，定时器回调中提交到工作队列执行
    outdoor_update_timer = xTimerCreate("outdoor update", pdMS_TO_TICKS(OUTDOOR_UPDATE_INTERVAL), pdTRUE, outdoor_update, work_timer_cb);

    // 第一次启动时，立即执行一提交
    workqueue_run(app_work, time_sync);
    workqueue_run(app_work, wifi_update);
    workqueue_run(app_work, inner_update);
    workqueue_run(app_work, outdoor_update);
    
    xTimerStart(time_update_timer, 0); // 启动时间更新定时器
    xTimerStart(time_sync_timer, 0); // 启动时间同步定时器
    xTimerStart(wifi_update_timer, 0); // 启动WIFI更新定时器
    xTimerStart(inner_update_timer, 0); // 启动内部更新定时器
    xTimerStart(outdoor_update_timer, 0); // 启动外部更新定时器
}
