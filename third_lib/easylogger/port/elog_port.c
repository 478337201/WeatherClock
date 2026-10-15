/*
 * This file is part of the EasyLogger Library.
 *
 * Copyright (c) 2015, Armink, <armink.ztl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Function: Portable interface for each platform.
 * Created on: 2015-04-28
 */
 
#include <elog.h>

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "stm32f4xx.h" // 标准外设库头文件
#include "console.h"
#include "rtc.h"

// 用于线程保护的互斥信号量句柄
static SemaphoreHandle_t s_log_mutex = NULL;


/**
 * EasyLogger port initialize
 *
 * @return result
 */
ElogErrCode elog_port_init(void) {
    ElogErrCode result = ELOG_NO_ERR;

    /* add your code here */
    s_log_mutex = xSemaphoreCreateMutex();
    
    return result;
}

/**
 * EasyLogger port deinitialize
 *
 */
void elog_port_deinit(void) {

    /* add your code here */
    if (s_log_mutex != NULL) 
    {
        vSemaphoreDelete(s_log_mutex);
    }
}

/**
 * output log port interface
 *
 * @param log output of log
 * @param size log size
 */
void elog_port_output(const char *log, size_t size) {
    
    /* add your code here */
    console_write(log, size);
}

/**
 * output lock
 */
void elog_port_output_lock(void) {
    
    /* add your code here */
    if (s_log_mutex != NULL) 
    {
        // 请求互斥信号量，若无法获取则一直等待
        xSemaphoreTake(s_log_mutex, portMAX_DELAY);
    }
}

/**
 * output unlock
 */
void elog_port_output_unlock(void) {
    
    /* add your code here */
    if (s_log_mutex != NULL) 
    {
        // 释放互斥信号量
        xSemaphoreGive(s_log_mutex);
    }
}

/**
 * get current time interface
 *
 * @return current time
 */
const char *elog_port_get_time(void) {
  //  
  //  /* add your code here */
  //  static char time_buf[16] = {0};
  //  TickType_t tick = xTaskGetTickCount();
  //  // 格式化为 秒.毫秒 的形式，configTICK_RATE_HZ 通常为1000
  //  snprintf(time_buf, sizeof(time_buf), "%lu.%03lu",
  //           tick / configTICK_RATE_HZ,
  //           tick % configTICK_RATE_HZ);
	static char time_buf[30] = {0};
 
	
	rtc_date_time_t data_time = {0};
	rtc_get_time(&data_time);
	snprintf(time_buf, sizeof(time_buf), "%04u-%02u-%02u %02u:%02u:%02u",
                    data_time.year,
                    data_time.month,
                    data_time.day,
                    data_time.hour,
                    data_time.minute,
                    data_time.second);

    return time_buf;
}

/**
 * get current process name interface
 *
 * @return current process name
 */
const char *elog_port_get_p_info(void) {
    
    /* add your code here */
    return "";
}

/**
 * get current thread name interface
 *
 * @return current thread name
 */
const char *elog_port_get_t_info(void) {
    
    /* add your code here */
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) 
    {
        return pcTaskGetName(xTaskGetCurrentTaskHandle());
    } 
    else 
    {
        return "";
    }
}
