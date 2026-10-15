#include "FreeRTOS.h"
#include "task.h"
#include "workqueue.h"
#include "app.h"
#include "ui.h"
#include "wifi.h"
#include "main_page.h"
#include "board.h"
#include "tcp.h"

/**
 * @brief 初始化任务
 * @param param 参数
 */
static void main_init(void *param)  
{
    board_init();  // 初始化片上外设（调试串口、RTC、AHT20）
    workqueue_init(); // 初始化工作队列（创建工作队列任务）
    ui_init();  // 初始化UI队列（创建UI渲染任务）
    ui_show_welcome();  // 显示欢迎页面
    wifi_init();  // 初始化WiFi
    ui_show_wifi(WIFI_SSID);  // 显示WiFi连接页面
    wifi_wait_connect();  // 等待WiFi连接成功
    tcp_init();  // 监听8080并读取IP，失败只记日志
    main_page_display();  // 显示主页面
    app_init();  // 初始化应用程序

    vTaskDelete(NULL);  // 初始化任务结束后要删除自己
}

int main(void)
{
    board_lowlevel_init();  // 初始化外设时钟
    xTaskCreate(main_init, "init", 1024, NULL, 9, NULL);  // 创建初始化任务
    vTaskStartScheduler();  // 启动调度器
    while (1) {}  // 代码不应该运行到这里
}
