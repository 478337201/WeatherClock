#include "workqueue.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <stdbool.h>

typedef struct
{
    work_t work;  
    void *param;  
} work_message_t;  // 工作消息结构体

static QueueHandle_t work_msg_queue;  // 工作队列句柄

/**
 * @brief 工作队列任务
 * @param param 任务参数
 */
static void work_func(void *param)  
{
    work_message_t msg;  
    
    while (1)
    {
        xQueueReceive(work_msg_queue, &msg, portMAX_DELAY);  // 从工作队列中取任务
        msg.work(msg.param);  // 执行任务
    }
}

/**
 * @brief 初始化工作队列
 */
void workqueue_init(void)  
{
    work_msg_queue = xQueueCreate(16, sizeof(work_message_t));  
    configASSERT(work_msg_queue);  
    xTaskCreate(work_func, "workqueue", 1024, NULL, 5, NULL);  // 创建工作队列任务
}

/**
 * @brief 将任务提交到工作队列
 * @param work 任务函数
 * @param param 任务参数
 */
void workqueue_run(work_t work, void *param)  
{
    configASSERT(work_msg_queue);
    work_message_t msg = { work, param };  
    xQueueSend(work_msg_queue, &msg, portMAX_DELAY);  // 将任务提交到工作队列（队列满时阻塞）
}

/**
 * @brief 把TCP任务提交到工作队列
 * @param work 任务函数
 * @param param 任务参数
 * @return 成功返回true
 */
bool workqueue_try(work_t work, void *param)
{
    if (work_msg_queue == NULL || work == NULL)  // 队列未初始化或任务函数为空
        return false;
    work_message_t msg = { work, param };  // 创建工作消息
    return xQueueSend(work_msg_queue, &msg, 0) == pdTRUE;  // 将任务提交到工作队列（队列满时不阻塞）
}