#include "tcp.h"
#include "esp_at.h"
#include "elog.h"
#include "board.h"
#include "workqueue.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define TCP_PORT 8080  // TCP端口号
#define TCP_PAYLOAD_MAX 240  // 最大业务数据字节数
#define TCP_FRAME_MAX (8 + TCP_PAYLOAD_MAX)  // 帧最大字节数（固定8字节帧头）

#define BIT_SNAPSHOT (1u << 0)  // 快照标记（全部数据）
#define BIT_TIME     (1u << 1)  // 时间标记
#define BIT_INDOOR   (1u << 2)  // 室内标记
#define BIT_OUTDOOR  (1u << 3)  // 室外标记
#define BIT_WIFI     (1u << 4)  // WiFi标记
#define BIT_PAGE     (1u << 5)  // 页面标记
#define BIT_HEART    (1u << 6)  // 心跳标记

static char device_ip[16] = "";  // 设备IP地址
static TimerHandle_t link_timer = NULL; // TCP监听定时器
static volatile uint32_t link_gen = 0;  // 已处理的链接代次
static uint32_t sent_gen_by_type[7];  // 该类型最近一次发送成功时的代次
static uint8_t send_fail_count[7];  // 当前代次里连续发送失败的次数
static uint32_t fail_gen = 0;  // send_fail_count 对应的代次
static volatile uint32_t pending_bits = 0;  // 存储待发位标记（只用低7位，为1表示对应标记）
static volatile bool flush_posted = false;  // 工作队列是否已有TCP传输任务
static uint16_t frame_seq = 0;  // 已经成功发出的帧序号（从0开始，循环递增）
static volatile uint32_t last_tx_tick = 0;  // 上次成功发送的时间戳
static volatile int8_t client_link = -1;  // 客户端TCP链路ID（-1表示无客户端）
static uint8_t frame_buf[TCP_FRAME_MAX] = {0};  // 帧缓冲区

static struct
{
    bool valid;  // 是否发送
    rtc_date_time_t value;  // RTC时间
} cache_time;  // 时间缓存

static struct
{
    bool valid;  // 是否发送
    float temperature;  // 温度
    float humidity;  // 湿度
} cache_indoor;  // 室内缓存

static struct
{
    bool valid;  // 是否发送
    int code;  // 天气代码
    float temperature;  // 温度
    char city[32];  // 城市
    char text[32];  // 天气描述
} cache_outdoor;  // 室外缓存

static struct
{
    bool valid;  // 是否发送
    bool connected;  // 连接状态
    int8_t rssi;  // 信号强度
    uint8_t channel;  // 信道
    char ssid[64];  // 网络名称
} cache_wifi;  // WiFi缓存

static struct
{
    bool valid;  // 是否发送
    uint8_t page;  // 页面编号
    char text[48];  // 页面说明
} cache_page;  // 页面缓存

/**
 * @brief 拷贝字符串，超长截断，结果带'\0'
 * @param dst 目标缓冲区
 * @param size 目标缓冲区大小
 * @param src 源字符串
 */
static void copy_text(char *dst, size_t size, const char *src)
{
    size_t len = 0;

    if (size == 0)
        return;
    if (src != NULL)
        len = strlen(src);
    if (len >= size)
        len = size - 1;
    if (len > 0)
        memcpy(dst, src, len);
    dst[len] = '\0';
}

/**
 * @brief 温度、湿度按0.1单位取整。26.4℃为264
 * @param value 值
 * @return 新的值
 */
static int16_t scale_x10(float value)
{
    return (int16_t)lroundf(value * 10.0f);
}

/**
 * @brief 写入8位无符号整数
 * @param buf 缓冲区
 * @param len 缓冲区索引
 * @param value 值
 * @return 新的缓冲区索引
 */
static size_t append_u8(uint8_t *buf, size_t len, uint8_t value)
{
    buf[len] = value;
    return len + 1;
}

/**
 * @brief 写入16位无符号整数
 * @param buf 缓冲区
 * @param len 缓冲区索引
 * @param value 值
 * @return 新的缓冲区索引
 */
static size_t append_u16(uint8_t *buf, size_t len, uint16_t value)
{
    buf[len] = (uint8_t)value;
    buf[len + 1] = (uint8_t)(value >> 8);
    return len + 2;
}

/**
 * @brief 写入32位无符号整数
 * @param buf 缓冲区
 * @param len 缓冲区索引
 * @param value 值
 * @return 新的缓冲区索引
 */
static size_t append_u32(uint8_t *buf, size_t len, uint32_t value)
{
    buf[len] = (uint8_t)value;
    buf[len + 1] = (uint8_t)(value >> 8);
    buf[len + 2] = (uint8_t)(value >> 16);
    buf[len + 3] = (uint8_t)(value >> 24);
    return len + 4;
}

/**
 * @brief 写入长度前缀字符串，不带'\0'。保证业务数据不超过240字节
 * @param buf 缓冲区
 * @param len 缓冲区索引
 * @param text 字符串
 * @return 新的缓冲区索引
 */
static size_t append_str(uint8_t *buf, size_t len, const char *text)
{
    size_t text_len = 0;

    if (len >= TCP_PAYLOAD_MAX)
        return len;
    if (text != NULL)
        text_len = strlen(text);
    if (text_len > 255)
        text_len = 255;
    if (len + 1 + text_len > TCP_PAYLOAD_MAX)
        text_len = TCP_PAYLOAD_MAX - len - 1;

    buf[len++] = (uint8_t)text_len;
    if (text_len > 0)
        memcpy(buf + len, text, text_len);
    return len + text_len;
}

/**
 * @brief 写入16位有符号整数
 * @param buf 缓冲区
 * @param len 缓冲区索引
 * @param value 值
 * @return 新的缓冲区索引
 */
static size_t append_i16(uint8_t *buf, size_t len, int16_t value)
{
    return append_u16(buf, len, (uint16_t)value);
}

typedef enum
{
    SEND_SKIP = 0,  // 没有可发的缓存
    SEND_OK,  // 已经交给模组
    SEND_FAIL,  // 链路还在，但这一帧没发出去
} send_result_t;

/**
 * @brief 组一帧并发出
 * @param type 帧类型
 * @param gen 这一帧要记到哪一代连接上
 * @return 跳过、成功或失败
 */
static send_result_t send_one(uint8_t type, uint32_t gen)
{
    uint8_t payload[TCP_PAYLOAD_MAX];  // 业务数据缓冲区
    size_t payload_len = 0;  // 缓冲区索引
    size_t total;  // 整帧字节数
    uint16_t crc;  // CRC累加器

    switch (type)
    {
        case 0x01:  // 心跳
        {
            uint32_t uptime_ms = (uint32_t)xTaskGetTickCount() * (uint32_t)portTICK_PERIOD_MS;
            payload_len = append_u16(payload, payload_len, frame_seq);  // 帧序号
            payload_len = append_u32(payload, payload_len, uptime_ms);  // 运行时间
            break;
        }
        case 0x02:  // 时间
            if (!cache_time.valid)  // 时间缓存无效
                return SEND_SKIP;
            payload_len = append_u16(payload, payload_len, frame_seq);  // 帧序号
            payload_len = append_u16(payload, payload_len, cache_time.value.year);  // 年
            payload_len = append_u8(payload, payload_len, cache_time.value.month);  // 月
            payload_len = append_u8(payload, payload_len, cache_time.value.day);  // 日
            payload_len = append_u8(payload, payload_len, cache_time.value.hour);  // 时
            payload_len = append_u8(payload, payload_len, cache_time.value.minute);  // 分
            payload_len = append_u8(payload, payload_len, cache_time.value.second);  // 秒
            payload_len = append_u8(payload, payload_len, cache_time.value.weekday);  // 星期
            break;
        case 0x03:  // 室内
            if (!cache_indoor.valid)  // 室内缓存无效
                return SEND_SKIP;
            payload_len = append_u16(payload, payload_len, frame_seq);  // 帧序号
            payload_len = append_i16(payload, payload_len, scale_x10(cache_indoor.temperature));  // 温度
            payload_len = append_u16(payload, payload_len, (uint16_t)scale_x10(cache_indoor.humidity));  // 湿度
            break;
        case 0x04:  // 室外
            if (!cache_outdoor.valid)  // 室外缓存无效
                return SEND_SKIP;
            payload_len = append_u16(payload, payload_len, frame_seq);  // 帧序号
            payload_len = append_i16(payload, payload_len, (int16_t)cache_outdoor.code);  // 天气代码
            payload_len = append_i16(payload, payload_len, scale_x10(cache_outdoor.temperature));  // 温度
            payload_len = append_str(payload, payload_len, cache_outdoor.city);  // 城市
            payload_len = append_str(payload, payload_len, cache_outdoor.text);  // 天气描述
            break;
        case 0x05:  // WiFi
            if (!cache_wifi.valid)  // WiFi缓存无效
                return SEND_SKIP;
            payload_len = append_u16(payload, payload_len, frame_seq);  // 帧序号
            payload_len = append_u8(payload, payload_len, cache_wifi.connected ? 1 : 0);  // 连接状态
            payload_len = append_u8(payload, payload_len, (uint8_t)cache_wifi.rssi);  // 信号强度
            payload_len = append_u8(payload, payload_len, cache_wifi.channel);  // 信道
            payload_len = append_str(payload, payload_len, cache_wifi.ssid);  // 网络名称
            break;
        case 0x06:  // 页面
            if (!cache_page.valid)  // 页面缓存无效
                return SEND_SKIP;
            payload_len = append_u16(payload, payload_len, frame_seq);  // 帧序号
            payload_len = append_u8(payload, payload_len, cache_page.page);  // 页面编号
            payload_len = append_str(payload, payload_len, cache_page.text);  // 页面说明
            break;
        default:
            return SEND_SKIP;
    }
    if (payload_len == 0 || payload_len > TCP_PAYLOAD_MAX)  // 业务数据长度无效
        return SEND_SKIP;

    total = 8 + payload_len;  // 整帧字节数
    if (sizeof(frame_buf) < total)
        return SEND_SKIP;

    // TCP帧格式：帧头（2B）+版本（1B）+类型（1B）+长度（2B）+业务数据（0-240B）+CRC（2B）
    frame_buf[0] = 0x55;  // 帧头：固定2B，用于标识帧起始
    frame_buf[1] = 0xAA;
    frame_buf[2] = 1;  // 版本：固定值，保证可扩展性
    frame_buf[3] = type;  // 类型：标识TCP传输的数据类型
    frame_buf[4] = (uint8_t)payload_len;  // 长度：业务数据字节数
    frame_buf[5] = (uint8_t)(payload_len >> 8);

    memcpy(frame_buf + 6, payload, payload_len);  // 业务数据：将业务数据拷贝到帧缓冲区

    crc = 0xFFFF;  // CRC-16校验/MODBUS（生成多项式为0x8005，逆序为0xA001）
    // 校验数据对生成多项式进行模2除法，得到余数即为CRC值
    for (size_t i = 0; i < 4 + payload_len; i++)  // 只校验版本+类型+长度+业务数据
    {
        crc ^= frame_buf[2 + i];  // 异或操作做减法
        for (int bit = 0; bit < 8; bit++)
        {
            if ((crc & 1u) != 0)  // 最低位为1，右移1位并异或0xA001
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            else  // 最低位为0，右移1位做除法
                crc >>= 1;
        }
    }
    frame_buf[6 + payload_len] = (uint8_t)crc;  // CRC：将CRC值写入帧缓冲区
    frame_buf[7 + payload_len] = (uint8_t)(crc >> 8);

    if (!esp_at_tcp_send(frame_buf, (uint16_t)total))  // 发送帧
        return SEND_FAIL;

    frame_seq++;  // 帧序号递增
    last_tx_tick = xTaskGetTickCount();  // 更新上次成功发送的时间戳
    if (type < sizeof(sent_gen_by_type) / sizeof(sent_gen_by_type[0]))
    {
        sent_gen_by_type[type] = gen;  // 这一代连接已经拿到该类型
        send_fail_count[type] = 0;
    }
    return SEND_OK;
}

/**
 * @brief 发送一种待发帧。失败时在同一代连接里最多再试两次
 * @return 需要稍后再试的位。0 表示不用再试
 */
static uint32_t transmit(uint32_t bits, uint32_t bit, uint8_t type, uint32_t gen)
{
    if ((bits & bit) == 0)
        return 0;
    if (send_one(type, gen) != SEND_FAIL)
        return 0;
    if (type >= sizeof(send_fail_count) || send_fail_count[type] >= 2)
    {
        if (type < sizeof(send_fail_count))
            send_fail_count[type] = 0;  // 留给下一次采样重新入队
        return 0;
    }
    send_fail_count[type]++;
    return bit;
}

/**
 * @brief 批次进行中如果新的一秒已经入队，马上发出，避免被后面的帧盖掉
 */
static uint32_t transmit_arrived_time(uint32_t gen)
{
    uint32_t due = 0;

    taskENTER_CRITICAL();
    if ((pending_bits & BIT_TIME) != 0)
    {
        pending_bits &= ~BIT_TIME;
        due = BIT_TIME;
    }
    taskEXIT_CRITICAL();
    if (due == 0)
        return 0;
    return transmit(due, BIT_TIME, 0x02, gen);
}

/**
 * @brief TCP传输任务
 */
static void tcp_send(void *param)
{
    for (;;)  // 把当前待发帧全部发送
    {
        uint32_t bits;  // 待发位标记
        uint32_t gen;  // 本次发送对应的连接代次
        int8_t link_id = -1;  // 初始化链路ID

        taskENTER_CRITICAL();
        bits = pending_bits;  // 读取待发位标记
        pending_bits = 0;  // 清空待发位标记
        taskEXIT_CRITICAL();

        esp_at_tcp_link_snapshot(&link_id, NULL);  // 读取TCP链路ID
        client_link = link_id;  // 更新客户端链路ID
        gen = link_gen;
        if (gen != fail_gen)  // 新连接重新计算失败次数
        {
            memset(send_fail_count, 0, sizeof(send_fail_count));
            fail_gen = gen;
        }
        if (link_id < 0)
            bits = 0;  // 没有客户端，清空待发位标记
        else
        {
            uint32_t retry = 0;  // 这次没发出去、还要再试的类型

            if ((bits & BIT_SNAPSHOT) != 0)  // 新连接先把已有缓存各发一帧
                bits |= BIT_TIME | BIT_INDOOR | BIT_OUTDOOR | BIT_WIFI | BIT_PAGE;
            bits &= ~BIT_SNAPSHOT;
            retry |= transmit(bits, BIT_TIME, 0x02, gen);  // 时间先发
            retry |= transmit(bits, BIT_INDOOR, 0x03, gen);  // 室内
            retry |= transmit_arrived_time(gen);  // 这一秒刚到就插进去，不排在后面
            retry |= transmit(bits, BIT_OUTDOOR, 0x04, gen);  // 室外
            retry |= transmit_arrived_time(gen);
            retry |= transmit(bits, BIT_WIFI, 0x05, gen);  // WiFi
            retry |= transmit_arrived_time(gen);
            retry |= transmit(bits, BIT_PAGE, 0x06, gen);  // 页面
            retry |= transmit_arrived_time(gen);
            if ((bits & BIT_HEART) != 0)  // 心跳失败等下一秒，不占着重试
                send_one(0x01, gen);

            taskENTER_CRITICAL();
            pending_bits |= retry;  // 失败的类型留给链路定时器再投递
            if (retry != 0)  // 避免发送失败时在这里空转
            {
                flush_posted = false;
                taskEXIT_CRITICAL();
                return;
            }
            taskEXIT_CRITICAL();
        }

        taskENTER_CRITICAL();
        if (link_id < 0)
            pending_bits = 0;  // 没有客户端，清空待发位标记
        if (pending_bits == 0 || link_id < 0)  // 没有待发帧或没有客户端
        {
            flush_posted = false;  // 清空TCP传输任务标志
            taskEXIT_CRITICAL();
            return;
        }
        taskEXIT_CRITICAL();
    }
}

/**
* @brief 投递TCP传输任务
*/
static void tcp_kick(void)
{
    bool post = false;  // 是否投递

    taskENTER_CRITICAL();
    if (pending_bits != 0 && !flush_posted)  // 有发送请求且没有TCP传输任务
    {
        flush_posted = true;  // 已有TCP传输任务
        post = true;  // 投递
    }
    taskEXIT_CRITICAL();

    if (post && !workqueue_try(tcp_send, NULL))  // 把TCP传输任务提交到工作队列
    {
        taskENTER_CRITICAL();
        flush_posted = false;
        taskEXIT_CRITICAL();
    }
}

/**
* @brief 请求TCP发送数据到客户端
* @param bit 待发位标记
*/
static void tcp_request(uint32_t bit)
{
    if (client_link < 0)  // 没有客户端
        return;

    taskENTER_CRITICAL();
    pending_bits |= bit;  // 置待发位
    taskEXIT_CRITICAL();
    tcp_kick();  // 投递发送请求任务
}

/**
* @brief 请求TCP发送数据到客户端
* @param type 数据类型
*/
static void publish_or_drop(uint8_t type)
{
    uint32_t bit = 0;  // 待发位标记

    switch (type)
    {
        case 0x02: bit = BIT_TIME; break;  // 时间
        case 0x03: bit = BIT_INDOOR; break;  // 室内
        case 0x04: bit = BIT_OUTDOOR; break;  // 室外
        case 0x05: bit = BIT_WIFI; break;  // WiFi
        case 0x06: bit = BIT_PAGE; break;  // 页面
        default: return;  // 数据类型无效
    }
    tcp_request(bit);  // 请求TCP发送数据到客户端
}

/**
 * @brief 当前连接是否已经成功收到过这种缓存
 */
static bool client_has_type(uint8_t type)
{
    return type < sizeof(sent_gen_by_type) / sizeof(sent_gen_by_type[0])
        && client_link >= 0
        && link_gen != 0
        && sent_gen_by_type[type] == link_gen;
}

/**
 * @brief 数值变了，或这条连接还没拿到缓存时，请求发送
 */
static void publish_fresh(uint8_t type, bool changed)
{
    if (changed || !client_has_type(type))
        publish_or_drop(type);
}

/**
 * @brief TCP监听定时器回调函数
 */
static void link_timer_cb(TimerHandle_t timer)
{
    static uint8_t stable_ticks;  // 页面、室外这类不常变化的数据的补发节拍
    int8_t link_id = -1;  // 初始化链路ID
    uint32_t gen = 0;  // 初始化代次
    esp_at_tcp_link_snapshot(&link_id, &gen);  // 读取TCP链路ID和代次
    client_link = link_id;  // 更新客户端链路ID
    if (link_id < 0)  // 没有客户端
    {
        stable_ticks = 0;
        taskENTER_CRITICAL();
        pending_bits = 0;  // 清空待发位
        taskEXIT_CRITICAL();
        return;
    }
    if (gen != link_gen)  // 代次不同
    {
        link_gen = gen;  // 更新代次
        stable_ticks = 0;
        tcp_request(BIT_SNAPSHOT);  // 新连接立刻发送已有缓存
        return;
    }

    if (++stable_ticks >= 10)  // 约2秒。只补这条连接还没收到的页面和室外
    {
        stable_ticks = 0;
        if (cache_page.valid && !client_has_type(0x06))
            tcp_request(BIT_PAGE);
        if (cache_outdoor.valid && !client_has_type(0x04))
            tcp_request(BIT_OUTDOOR);  // 现象文本在这一帧里
    }
    tcp_kick();  // 投递TCP传输任务
}

/**
 * @brief 开启TCP监听
 */
void tcp_init(void)
{
    if (!esp_at_tcp_listen(TCP_PORT))  // 建立TCP服务器
    {
    #if IS_DEBUG_VERSION
        elog_e("TCP", "listen %u failed\r\n", (unsigned)TCP_PORT);
    #endif
        return;
    }

    if (!esp_at_tcp_get_ip(device_ip, sizeof(device_ip)) || device_ip[0] == '\0')  // 读取IP地址
    {
        device_ip[0] = '\0';
#if IS_DEBUG_VERSION
        elog_e("TCP", "get ip failed\r\n");
#endif
        return;
    }

#if IS_DEBUG_VERSION
    elog_i("TCP", "listen %s:%u\r\n", device_ip, (unsigned)TCP_PORT);
#endif

    link_timer = xTimerCreate("tcp link", pdMS_TO_TICKS(200), pdTRUE, NULL, link_timer_cb);  // 创建定时器监听
    if (link_timer == NULL || xTimerStart(link_timer, 0) != pdPASS)  // 启动定时器
    {
#if IS_DEBUG_VERSION
        elog_e("TCP", "link timer failed\r\n");
#endif
    }
}

/**
 * @brief 获取IP地址
 * @return IP地址，失败时是空串
 */
const char *tcp_ip(void)
{
    return device_ip;
}

/**
* @brief 请求TCP发送心跳数据到客户端（说明TCP连接正常）
*/
void tcp_on_second(void)
{
    uint32_t now;  // 当前时间
    uint32_t last;  // 上次成功发送时间

    if (client_link < 0)  // 没有客户端
        return;

    taskENTER_CRITICAL();
    if (pending_bits != 0 || flush_posted)  // 有待发帧或已有发送任务
    {
        taskEXIT_CRITICAL();
        return;
    }
    taskEXIT_CRITICAL();

    now = xTaskGetTickCount();  // 当前时间
    last = last_tx_tick;  // 上次成功发送时间
    if (last != 0 && (uint32_t)(now - last) < pdMS_TO_TICKS(1000))
        return;  // 上次成功发送时间未满1秒

    tcp_request(BIT_HEART);  // 请求TCP发送心跳数据到客户端
}

/**
 * @brief 请求TCP发送时间数据到客户端
 * @param time 时间
 */
void tcp_cache_time(const rtc_date_time_t *time)
{
    if (time == NULL || time->year < 2020)  // 时间无效
        return;

    cache_time.value = *time;  // 缓存时间
    cache_time.valid = true;  // 标记为有效
    publish_or_drop(0x02);  // 请求TCP发送时间数据到客户端
}

/**
 * @brief 请求TCP发送室内数据到客户端
 * @param temperature 温度
 * @param humidity 湿度
 */
void tcp_cache_indoor(float temperature, float humidity)
{
    bool changed = !cache_indoor.valid
        || cache_indoor.temperature != temperature
        || cache_indoor.humidity != humidity;

    cache_indoor.temperature = temperature;  // 缓存温度
    cache_indoor.humidity = humidity;  // 缓存湿度
    cache_indoor.valid = true;  // 标记为有效
    publish_fresh(0x03, changed);  // 有变化，或这条连接还没收到，才发送
}

/**
 * @brief 请求TCP发送室外数据到客户端
 * @param city 城市
 * @param text 天气描述
 * @param code 天气代码
 * @param temperature 温度
 */
void tcp_cache_outdoor(const char *city, const char *text, int code, float temperature)
{
    char city_buf[sizeof(cache_outdoor.city)];
    char text_buf[sizeof(cache_outdoor.text)];
    bool changed;

    copy_text(city_buf, sizeof(city_buf), city);  // 先按缓存长度截断，再和上次比较
    copy_text(text_buf, sizeof(text_buf), text);
    changed = !cache_outdoor.valid
        || cache_outdoor.code != code
        || cache_outdoor.temperature != temperature
        || strcmp(cache_outdoor.city, city_buf) != 0
        || strcmp(cache_outdoor.text, text_buf) != 0;
    memcpy(cache_outdoor.city, city_buf, sizeof(city_buf));  // 缓存城市
    memcpy(cache_outdoor.text, text_buf, sizeof(text_buf));  // 缓存天气描述
    cache_outdoor.code = code;  // 缓存天气代码
    cache_outdoor.temperature = temperature;  // 缓存温度
    cache_outdoor.valid = true;  // 标记为有效
    publish_fresh(0x04, changed);  // 有变化，或这条连接还没收到，才发送
}

/**
 * @brief 请求TCP发送WiFi数据到客户端
 * @param connected 连接状态
 * @param ssid 网络名称
 * @param rssi 信号强度
 * @param channel 信道
 */
void tcp_cache_wifi(bool connected, const char *ssid, int8_t rssi, uint8_t channel)
{
    char ssid_buf[sizeof(cache_wifi.ssid)];
    bool changed;

    copy_text(ssid_buf, sizeof(ssid_buf), ssid);  // 先按缓存长度截断，再和上次比较
    changed = !cache_wifi.valid
        || cache_wifi.connected != connected
        || strcmp(cache_wifi.ssid, ssid_buf) != 0
        || (connected && (cache_wifi.rssi != rssi || cache_wifi.channel != channel));
    memcpy(cache_wifi.ssid, ssid_buf, sizeof(ssid_buf));  // 缓存网络名称
    cache_wifi.connected = connected;  // 缓存连接状态
    if (connected)
    {
        cache_wifi.rssi = rssi;  // 缓存信号强度
        cache_wifi.channel = channel;  // 缓存信道
    }
    cache_wifi.valid = true;  // 标记为有效
    publish_fresh(0x05, changed);  // 有变化，或这条连接还没收到，才发送
}

/**
 * @brief 请求TCP发送页面数据到客户端
 * @param page 页面编号
 * @param text 页面说明
 */
void tcp_cache_page(uint8_t page, const char *text)
{
    char text_buf[sizeof(cache_page.text)];
    bool changed;

    if (page != TCP_PAGE_MAIN && page != TCP_PAGE_ERROR)  // 页面编号无效
        return;

    copy_text(text_buf, sizeof(text_buf), text);
    changed = !cache_page.valid
        || cache_page.page != page
        || strcmp(cache_page.text, text_buf) != 0;
    cache_page.page = page;  // 缓存页面编号
    memcpy(cache_page.text, text_buf, sizeof(text_buf));  // 缓存页面说明
    cache_page.valid = true;  // 标记为有效
    publish_fresh(0x06, changed);  // 有变化，或这条连接还没收到，才发送
}
