#include "esp_at.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "stm32f4xx.h"
#include "elog.h"
#include "board.h"

#include <string.h>
#include <stdio.h>

typedef enum
{
    AT_ACK_NONE,  // 无响应
    AT_ACK_OK,  // 响应OK
    AT_ACK_ERROR,  // 响应ERROR
    AT_ACK_BUSY,  // 响应BUSY
    AT_ACK_READY,  // 响应READY
    AT_ACK_PROMPT,  // 定长发送开始
    AT_ACK_SEND_OK,  // 定长发送完成
} at_ack_t;  // AT命令响应状态

typedef enum
{
    AT_RX_LINE,  // 普通AT命令（中断按\n切行，at_ack_matches）
    AT_RX_PROMPT,  // 定长发送开始（中断等待提示符>）
    AT_RX_SEND_RESULT,  // 定长发送结束（中断等待at_send_matches）
} at_rx_mode_t;  // 接收模式（不同模式中断处理方式不同）

typedef enum
{
    ESP_AT_LINK_KEEP = 0,  // 不改链路
    ESP_AT_LINK_CONNECT,  // 收到 <id>,CONNECT
    ESP_AT_LINK_CLOSED,  // 收到 <id>,CLOSED
} esp_at_link_op_t;

typedef struct
{
    at_ack_t ack;  // AT命令响应状态
    const char *string;  // 响应字符串
} at_ack_match_t;  // AT命令响应匹配

static const at_ack_match_t at_ack_matches[] = 
{
    {AT_ACK_OK, "OK\r\n"},
    {AT_ACK_ERROR, "ERROR\r\n"},
    {AT_ACK_BUSY, "busy p...\r\n"},
    {AT_ACK_READY, "ready\r\n"},
};  // AT命令响应匹配表
static const at_ack_match_t at_send_matches[] =
{
    {AT_ACK_SEND_OK, "SEND OK\r\n"},
    {AT_ACK_ERROR, "SEND FAIL\r\n"},
    {AT_ACK_ERROR, "ERROR\r\n"},
};  // 定长发送的结束行。单独一行OK不算发送完成

static char rxbuf[1024];  // 只接收模组发来的字节
static char txbuf[1024];  // 串口的发送缓冲区
static char *rxline = rxbuf;  // 指向接收缓冲区的当前位置
static uint32_t rxlen = 0;  // 已经写入缓冲区的字节数
static at_ack_t rxack = AT_ACK_NONE;  // 当前AT命令的响应状态
static volatile at_rx_mode_t rx_mode = AT_RX_LINE;  // 接收模式
static volatile int8_t tcp_link_id = -1;  // TCP链接ID，-1表示没有客户端
static volatile uint32_t tcp_link_gen = 0;  // TCP链接代次，每收到一次CONNECT加1
static SemaphoreHandle_t at_ack_sempahore;  // 信号量，用于等待AT命令响应

/**
 * @brief 清接收缓冲并更新链路。调用前必须已经进入临界区
 * @param clear_rx 为 true 时丢掉半行并切换接收模式
 * @param mode 清缓冲后使用的接收模式
 * @param link_op 本次要不要改链路
 * @param link_id 十进制链路号，仅 CONNECT/CLOSED 使用
 */
static void esp_at_state_apply(bool clear_rx, at_rx_mode_t mode, esp_at_link_op_t link_op, int link_id)
{
    if (clear_rx)  // 命令超时或下一条命令开始：丢掉半行
    {
        rx_mode = mode;
        rxlen = 0;
        rxline = rxbuf;
        rxbuf[0] = '\0';
        rxack = AT_ACK_NONE;
    }

    if (link_op == ESP_AT_LINK_CONNECT)  // 行模式里的 <id>,CONNECT，不当成 OK
    {
        tcp_link_id = (int8_t)link_id;
        tcp_link_gen++;
    }
    else if (link_op == ESP_AT_LINK_CLOSED)  // 只清掉当前这条链路
    {
        if (tcp_link_id == (int8_t)link_id)
            tcp_link_id = -1;
    }
}

/**
 * @brief 切换接收模式并丢掉尚未成行的字节
 * @param mode 接下来使用的接收模式
 */
static void esp_at_prepare_receive(at_rx_mode_t mode)
{
    taskENTER_CRITICAL();  // 与 USART2 中断里的 esp_at_state_apply 互斥
    esp_at_state_apply(true, mode, ESP_AT_LINK_KEEP, 0);
    for (uint32_t n = 0; n < 32 && USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == SET; n++)
        (void)USART_ReceiveData(USART2);
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) == SET)
        (void)USART_ReceiveData(USART2);
    while (xSemaphoreTake(at_ack_sempahore, 0) == pdPASS) {}
    taskEXIT_CRITICAL();
}

/**
 * @brief 等待当前模式下的AT响应
 * @param timeout 超时时间
 * @return 响应状态。超时返回AT_ACK_NONE，不清空缓冲区
 */
static at_ack_t esp_at_usart_wait_receive(uint32_t timeout)
{
    bool acked = xSemaphoreTake(at_ack_sempahore, pdMS_TO_TICKS(timeout)) == pdPASS;
    return acked ? rxack : AT_ACK_NONE;
}

/**
 * @brief 判断一行是不是TCP主动上报
 * @param line 以\r结尾的当前行
 * @param tag CONNECT或CLOSED
 * @param id 解析出的链路号
 * @return 整行匹配时返回true
 */
static bool esp_at_line_is_urc(const char *line, const char *tag, int *id)
{
    int value = 0;
    const char *p = line;

    if (*p < '0' || *p > '9')
        return false;

    while (*p >= '0' && *p <= '9')
    {
        value = value * 10 + (*p - '0');
        if (value > 9)
            return false;
        p++;
    }

    if (*p++ != ',')
        return false;

    while (*tag != '\0')
    {
        if (*p++ != *tag++)
            return false;
    }

    if (*p != '\r')
        return false;

    *id = value;
    return true;
}

/**
 * @brief 中断里认出 CONNECT/CLOSED。定长发送期间模组仍会插进这两行
 * @param line 以 \r 结尾的当前行
 * @return 是链路上报时返回 true，不当成命令结果
 */
static bool esp_at_take_link_urc_from_isr(const char *line)
{
    int id = -1;
    esp_at_link_op_t op;
    UBaseType_t mask;

    if (esp_at_line_is_urc(line, "CONNECT", &id))
        op = ESP_AT_LINK_CONNECT;
    else if (esp_at_line_is_urc(line, "CLOSED", &id))
        op = ESP_AT_LINK_CLOSED;
    else
        return false;

    mask = taskENTER_CRITICAL_FROM_ISR();
    esp_at_state_apply(false, rx_mode, op, id);  // 只更新链路，不当成 OK
    taskEXIT_CRITICAL_FROM_ISR(mask);
    return true;
}

/**
 * @brief 从中断唤醒正在等待的命令
 * @param ack 本次要交给等待者的结果
 */
static void esp_at_signal_ack_from_isr(at_ack_t ack)
{
    BaseType_t woken = pdFALSE;
    rxack = ack;  // 设置本次响应状态
    xSemaphoreGiveFromISR(at_ack_sempahore, &woken);  // 释放信号量
    portYIELD_FROM_ISR(woken);  // 从ISR中唤醒等待者
}

/**
 * @brief DMA发送定长数据
 * @param data 待发送数据
 * @param len 字节数
 * @return 成功返回true
 */
static bool esp_at_dma_send(const void *data, uint32_t len)
{
    TickType_t start;

    if (data == NULL || len == 0)
        return false;

    DMA_Cmd(DMA1_Stream6, DISABLE);  // 上一次传输没结束时不能改地址和长度
    start = xTaskGetTickCount();
    while (DMA_GetCmdStatus(DMA1_Stream6) != DISABLE)
    {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(100))
            return false;
    }

    DMA1_Stream6->M0AR = (uint32_t)data;
    DMA1_Stream6->NDTR = len;
    DMA_ClearFlag(DMA1_Stream6, DMA_FLAG_TCIF6);
    DMA_Cmd(DMA1_Stream6, ENABLE);

    start = xTaskGetTickCount();
    while (DMA_GetFlagStatus(DMA1_Stream6, DMA_FLAG_TCIF6) == RESET)
    {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(10000))
            return false;
    }
    return true;
}

/**
 * @brief 发送AT命令
 * @param command 命令字符串
 * @param timeout 超时时间
 * @return 是否成功
 */
static bool esp_at_write_command(const char *command, uint32_t timeout)
{
    at_ack_t ack = AT_ACK_NONE;

#if IS_DEBUG_VERSION
    elog_d("Driver_ESP","Send: %s", command);
#endif

    esp_at_prepare_receive(AT_RX_LINE);  // 先进入行模式并丢掉半行，再启动DMA
    if (esp_at_dma_send(command, strlen(command)))
        ack = esp_at_usart_wait_receive(timeout);  // 等待AT命令响应

#if IS_DEBUG_VERSION
    elog_d("Driver_ESP","Response:%s", rxbuf);
#endif

    if (ack != AT_ACK_OK)
        esp_at_prepare_receive(AT_RX_LINE);  // 超时或失败后丢掉残留应答

    return ack == AT_ACK_OK;
}

/**
 * @brief 初始化USART（与ESP32通信）
 * @return 成功返回true，失败返回false
 */
bool esp_at_init(void)
{
    at_ack_sempahore = xSemaphoreCreateBinary();  // 创建信号量
    configASSERT(at_ack_sempahore);
    
    // 初始化USART
    USART_InitTypeDef USART_InitStructure;
    USART_StructInit(&USART_InitStructure);
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_Init(USART2, &USART_InitStructure);
    USART_DMACmd(USART2, USART_DMAReq_Tx, ENABLE);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);  // 使能串口接收中断
    USART_Cmd(USART2, ENABLE);

    // 初始化DMA
    DMA_InitTypeDef DMA_InitStruct;
    DMA_StructInit(&DMA_InitStruct);
    DMA_InitStruct.DMA_Channel = DMA_Channel_4;
    DMA_InitStruct.DMA_PeripheralBaseAddr = (uint32_t)&USART2->DR;
    DMA_InitStruct.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    DMA_InitStruct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStruct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStruct.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStruct.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStruct.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStruct.DMA_Priority = DMA_Priority_Medium;
    DMA_InitStruct.DMA_FIFOMode = DMA_FIFOMode_Enable;
    DMA_InitStruct.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStruct.DMA_MemoryBurst = DMA_MemoryBurst_INC8;
    DMA_InitStruct.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_Init(DMA1_Stream6, &DMA_InitStruct);

    // 初始化中断
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    NVIC_SetPriority(USART2_IRQn, 5);

    // 初始化USART GPIO
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // 等待ESP AT启动（AT命令响应）
    bool booted = false;
    for (int t = 0; t < 20; t++)
    {
        if (esp_at_write_command("AT\r\n", 100))  // 检查是否能够正常通信
        {
            booted = true;
            break;
        }
    }
    if (booted == false)
        return false;

    if (!esp_at_write_command("AT+RESTORE\r\n", 2000))  // 重启ESP
        return false;

    /* OK 之后模组还会上报 ready。这里不清缓冲，避免把已经收到的 ready 丢掉。 */
    if (esp_at_usart_wait_receive(5000) != AT_ACK_READY)
    {
        esp_at_prepare_receive(AT_RX_LINE);
        return false;
    }
    
    return true;
}

/**
* @brief 初始化WIFI
* @return 是否成功
*/
bool esp_at_wifi_init(void)
{
    return esp_at_write_command("AT+CWMODE=1\r\n", 2000);
}

/**
* @brief 连接WIFI
* @param ssid WIFI名称
* @param pwd WIFI密码
* @param mac WIFI MAC地址
* @return 是否成功
*/
bool esp_at_connect_wifi(const char *ssid, const char *pwd, const char *mac)
{
    if (ssid == NULL || pwd == NULL)
        return false;

    if (mac)
        snprintf(txbuf, sizeof(txbuf), "AT+CWJAP=\"%s\",\"%s\",\"%s\"\r\n", ssid, pwd, mac);
    else
        snprintf(txbuf, sizeof(txbuf), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);

    return esp_at_write_command(txbuf, 5000);  // 发送AT命令
}

/**
* @brief 获取WIFI信息
* @param info 指向WIFI信息的指针
* @return 是否成功
*/
bool esp_at_get_wifi_info(esp_wifi_info_t *info)
{
    if (!esp_at_write_command("AT+CWSTATE?\r\n", 2000))  // 发送查询命令
        return false;

    // 解析响应
    const char *response = strstr(rxbuf, "+CWSTATE:");
    if (response == NULL)
        return false;
    int wifi_state;
    if (sscanf(response, "+CWSTATE:%d,\"%63[^\"]", &wifi_state, info->ssid) != 2)
        return false;
    info->connected = (wifi_state == 2);
    info->rssi = 0;
    info->channel = 0;
    return true;
}

/**
 * @brief 跳过 +CWJAP 应答里的一个字段。带引号时引号内的逗号不算分隔
 */
static const char *esp_at_skip_cwjap_field(const char *text)
{
    if (text == NULL)
        return NULL;
    if (*text == '"')
    {
        text++;
        while (*text != '\0' && *text != '"')
            text++;
        if (*text == '"')
            text++;
    }
    else
    {
        while (*text != '\0' && *text != ',')
            text++;
    }
    if (*text != ',')
        return NULL;
    return text + 1;
}

/**
 * @brief 查询当前AP的信道和RSSI
 * @param rssi 输出信号强度
 * @param channel 输出信道
 * @return 解析成功返回true。失败时不改连接状态
 */
bool esp_at_get_ap_info(int8_t *rssi, uint8_t *channel)
{
    const char *response;
    int ap_channel;
    int ap_rssi;

    if (rssi == NULL || channel == NULL)
        return false;
    if (!esp_at_write_command("AT+CWJAP?\r\n", 2000))
        return false;

    response = strstr(rxbuf, "+CWJAP:");
    if (response == NULL)
        return false;
    response = esp_at_skip_cwjap_field(response + 7);
    response = esp_at_skip_cwjap_field(response);
    if (response == NULL || sscanf(response, "%d,%d", &ap_channel, &ap_rssi) != 2)
        return false;
    if (ap_channel < 0 || ap_channel > 255 || ap_rssi < -128 || ap_rssi > 127)
        return false;

    *channel = (uint8_t)ap_channel;
    *rssi = (int8_t)ap_rssi;
    return true;
}

/**
* @brief 初始化SNTP
* @return 是否成功
*/
bool esp_at_sntp_init(void)
{
    if (!esp_at_write_command("AT+CIPSNTPCFG=1,8\r\n", 2000))
        return false;
    return true;
}

/**
* @brief 获取网络时间
* @param date 指向网络时间的指针
* @return 是否成功
*/
bool esp_at_sntp_get_time(esp_date_time_t *date)
{
    if (!esp_at_write_command("AT+CIPSNTPTIME?\r\n", 2000))  // 发送查询命令
        return false;

    // 解析响应
    char weekday_str[8];
    char month_str[4];
    const char *response = strstr(rxbuf, "+CIPSNTPTIME:");
    if (sscanf(response, "+CIPSNTPTIME:%3s %3s %hhu %hhu:%hhu:%hhu %hu",
        weekday_str, month_str, &date->day, &date->hour, &date->minute,
        &date->second, &date->year) != 7)
        return false;

    date->weekday = 0;
    const char *weekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    for (uint8_t i = 0; i < 7; i++)
    {
        if (strcmp(weekday_str, weekdays[i]) == 0)
        {
            date->weekday = i + 1;
            break;
        }
    }

    date->month = 0;
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (uint8_t i = 0; i < 12; i++)
    {
        if (strcmp(month_str, months[i]) == 0)
        {
            date->month = i + 1;
            break;
        }
    }
    return true;
}

/**
* @brief 发送HTTP GET请求
* @param url URL地址
* @return 响应数据
*/
const char *esp_at_http_get(const char *url)
{
    snprintf(txbuf, sizeof(txbuf), "AT+HTTPCLIENT=2,1,\"%s\",,,2\r\n", url);
    bool ret = esp_at_write_command(txbuf, 5000);
    return ret ? rxbuf : NULL;
}

/**
 * @brief 建立TCP服务器
 * @param port 端口号
 * @return 三步都成功返回true
 */
bool esp_at_tcp_listen(uint16_t port)
{
    if (!esp_at_write_command("AT+CIPMUX=1\r\n", 2000))  // 设置多连接模式（支持多路复用）
        return false;
    if (!esp_at_write_command("AT+CIPSERVERMAXCONN=1\r\n", 2000))  // 设置最大连接数为1
        return false;

    // 建立TCP服务器，监听指定端口
    snprintf(txbuf, sizeof(txbuf), "AT+CIPSERVER=1,%u\r\n", (unsigned)port);
    return esp_at_write_command(txbuf, 2000);  // 发送命令
}

/**
 * @brief 读取IP地址
 * @param ip 输出IP地址
 * @param size IP地址大小
 * @return 成功返回true
 */
bool esp_at_tcp_get_ip(char *ip, size_t size)
{
    const char *p;
    unsigned octet[4];
    int wrote;

    if (ip == NULL || size == 0)  // 参数无效
        return false;

    ip[0] = '\0';  // 初始化IP地址
    if (!esp_at_write_command("AT+CIPSTA?\r\n", 2000))  // 查询IP
        return false;

    p = strstr(rxbuf, "+CIPSTA:ip:");  // 从应答里定位IP行
    if (p == NULL)  // 没有找到IP行
        return false;

    p += sizeof("+CIPSTA:ip:") - 1;  // 跳过IP行前缀
    if (*p == '"')  // 跳过引号
        p++;

    for (int i = 0; i < 4; i++)  // 循环读取四段IP地址
    {
        unsigned value = 0;
        int digits = 0;

        if (*p < '0' || *p > '9')  // 起始不是数字
            return false;

        while (*p >= '0' && *p <= '9')  // 读取数字
        {
            value = value * 10u + (unsigned)(*p - '0');
            if (value > 255 || ++digits > 3)  // 超过255或超过3位
                return false;
            p++;
        }
        octet[i] = value;  // 保存IP地址
        if (i < 3)
        {
            if (*p != '.')  // 不是点
                return false;
            p++;
        }
    }

    if (*p != '\0' && *p != '"' && *p != '\r' && *p != '\n' && *p != ',' && *p != ' ')  // 不是结束符
        return false;

    wrote = snprintf(ip, size, "%u.%u.%u.%u", octet[0], octet[1], octet[2], octet[3]);  // 格式化IP地址
    if (wrote < 0 || (size_t)wrote >= size)  // 缓冲装不下点分地址
    {
        ip[0] = '\0';  // 初始化IP地址
        return false;
    }
    return true;  // 成功
}

/**
 * @brief   // TCP定长发送数据
 * @param data 发送的数据
 * @param len 字节数
 * @return 收到SEND OK返回true
 */
bool esp_at_tcp_send(const uint8_t *data, uint16_t len)
{
    int8_t link_id = -1;  // 链路ID
    at_ack_t ack;  // 响应状态

    if (data == NULL || len == 0)  // 参数无效
        return false;

    esp_at_tcp_link_snapshot(&link_id, NULL);  // 获取链路ID
    if (link_id < 0)  // 没有客户端
        return false;

    snprintf(txbuf, sizeof(txbuf), "AT+CIPSEND=%d,%u\r\n", (int)link_id, (unsigned)len);  // 拼接启动发送命令
    esp_at_prepare_receive(AT_RX_PROMPT);  // 切换到接收提示符模式
    if (!esp_at_dma_send(txbuf, strlen(txbuf)) || esp_at_usart_wait_receive(5000) != AT_ACK_PROMPT)  // 发送命令并等待提示符
    {
        esp_at_prepare_receive(AT_RX_LINE);  // 切换到行模式
        return false;
    }

    esp_at_prepare_receive(AT_RX_SEND_RESULT);  // 切换到发送结果模式
    if (!esp_at_dma_send(data, len))  // 发送数据
    {
        esp_at_prepare_receive(AT_RX_LINE);  // 切换到行模式
        return false;
    }

    ack = esp_at_usart_wait_receive(5000);  // 等待发送结果
    if (ack != AT_ACK_SEND_OK)  // 发送失败
        esp_at_prepare_receive(AT_RX_LINE);
    return ack == AT_ACK_SEND_OK;  // 发送成功返回true
}

/**
* @brief 读取TCP链路ID和代次
* @param link_id 链路ID
* @param generation CONNECT代次
*/
void esp_at_tcp_link_snapshot(int8_t *link_id, uint32_t *generation)
{
    taskENTER_CRITICAL();
    if (link_id != NULL)
        *link_id = tcp_link_id; // 链路ID
    if (generation != NULL)
        *generation = tcp_link_gen; // 代次
    taskEXIT_CRITICAL();
}

/**
* @brief USART2中断服务函数（串口每收到一个字节时触发）
*/
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) == SET)  // 接收数据寄存器收到1个字节
    {
        uint8_t byte = (uint8_t)USART_ReceiveData(USART2);  // 读DR，避免未读时RXNE一直挂起
        at_rx_mode_t mode = rx_mode;
        at_ack_t signal = AT_ACK_NONE;

        if (mode == AT_RX_PROMPT)  // 找到 > 之前，换行不是命令结束
        {
            if (byte == '>')
            {
                if (rxlen < sizeof(rxbuf) - 1)
                    rxbuf[rxlen++] = (char)byte;
                signal = AT_ACK_PROMPT;
            }
            else if (rxlen < sizeof(rxbuf) - 1)
            {
                rxbuf[rxlen++] = (char)byte;
                if (byte == '\n')  // 等提示符时仍可能插入 0,CONNECT / 0,CLOSED
                {
                    rxbuf[rxlen] = '\0';
                    esp_at_take_link_urc_from_isr(rxline);
                    rxline = rxbuf + rxlen;
                }
            }
        }
        else if (rxlen < sizeof(rxbuf) - 1)  // 行模式或发送完成模式
        {
            const at_ack_match_t *table = NULL;
            uint32_t count = 0;

            rxbuf[rxlen++] = (char)byte;
            if (byte == '\n')
            {
                rxbuf[rxlen] = '\0';

                if (!esp_at_take_link_urc_from_isr(rxline))  // 行模式和等待 SEND OK 时都认连接变化
                {
                    if (mode == AT_RX_LINE)
                    {
                        table = at_ack_matches;
                        count = sizeof(at_ack_matches) / sizeof(at_ack_matches[0]);
                    }
                    else if (mode == AT_RX_SEND_RESULT)  // 只等 SEND OK 或 ERROR
                    {
                        table = at_send_matches;
                        count = sizeof(at_send_matches) / sizeof(at_send_matches[0]);
                    }

                    for (uint32_t i = 0; i < count; i++)
                    {
                        if (strcmp(rxline, table[i].string) == 0)
                        {
                            signal = table[i].ack;
                            break;
                        }
                    }
                }

                rxline = rxbuf + rxlen;
            }
        }

        if (signal != AT_ACK_NONE)
            esp_at_signal_ack_from_isr(signal);

        USART_ClearITPendingBit(USART2, USART_IT_RXNE);  // 清除接收数据寄存器中断标志
    }
}
