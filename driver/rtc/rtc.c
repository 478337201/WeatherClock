#include "rtc.h"
#include "stm32f4xx.h"

#include <string.h>

/**
 * @brief 获取RTC时间一次
 * @param date_time 时间
 */
static void _rtc_get_time_once(rtc_date_time_t *date_time)
{
    RTC_DateTypeDef date;
    RTC_TimeTypeDef time;
    RTC_DateStructInit(&date);
    RTC_TimeStructInit(&time);
    RTC_GetDate(RTC_Format_BIN, &date);
    RTC_GetTime(RTC_Format_BIN, &time);
    date_time->year = 2000 + date.RTC_Year;
    date_time->month = date.RTC_Month;
    date_time->day = date.RTC_Date;
    date_time->weekday = date.RTC_WeekDay;
    date_time->hour = time.RTC_Hours;
    date_time->minute = time.RTC_Minutes;
    date_time->second = time.RTC_Seconds;
}

/**
 * @brief RTC初始化
 */
void rtc_init(void)
{
    RTC_InitTypeDef RTC_InitStruct;  
    RTC_StructInit(&RTC_InitStruct);
    RTC_Init(&RTC_InitStruct);  // RTC初始化
    
    RCC_RTCCLKCmd(ENABLE);  // 使能RTC时钟
    RTC_WaitForSynchro();  // 等待RTC时钟同步
}

/**
 * @brief 设置RTC时间
 * @param date_time 时间
 */
void rtc_set_time(const rtc_date_time_t *date_time)
{
    rtc_date_time_t rtime;
    do
    {
        // 设置RTC日期和时间
        RTC_DateTypeDef date;  // RTC日期
        RTC_TimeTypeDef time;  // RTC时间
        RTC_DateStructInit(&date);
        RTC_TimeStructInit(&time);
        date.RTC_Year = date_time->year - 2000;  // RTC年份寄存器的值为0~99
        date.RTC_Month = date_time->month;
        date.RTC_Date = date_time->day;
        date.RTC_WeekDay = date_time->weekday;
        time.RTC_Hours = date_time->hour;
        time.RTC_Minutes = date_time->minute;
        time.RTC_Seconds = date_time->second;
        RTC_SetDate(RTC_Format_BIN, &date);  // 设置RTC日期
        RTC_SetTime(RTC_Format_BIN, &time);  // 设置RTC时间

        _rtc_get_time_once(&rtime);  // 获取RTC时间
    } while (date_time->second != rtime.second);  // 至少执行一次（如果时间不一致，则继续执行）
}

/**
 * @brief 获取RTC时间
 * @param date_time 时间
 */
void rtc_get_time(rtc_date_time_t *date_time)
{
    rtc_date_time_t time1, time2;
    do
    {
        _rtc_get_time_once(&time1);  // 获取RTC时间
        _rtc_get_time_once(&time2);  // 获取RTC时间
    } while (memcmp(&time1, &time2, sizeof(rtc_date_time_t)) != 0);  // 如果时间不一致，则继续执行
    memcpy(date_time, &time1, sizeof(rtc_date_time_t));  // 复制RTC时间到date_time
}
