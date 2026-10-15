#ifndef __APP_WORKQUEUE_H__
#define __APP_WORKQUEUE_H__

#include <stdbool.h>

typedef void (*work_t)(void *param);

void workqueue_init(void); // 初始化工作队列
void workqueue_run(work_t work, void *param); // 将任务提交到工作队列
bool workqueue_try(work_t work, void *param);  // 把TCP任务提交到工作队列

#endif  // __APP_WORKQUEUE_H__
