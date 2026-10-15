#ifndef __AHT20_H__
#define __AHT20_H__

#include <stdbool.h>

bool aht20_init(void);  // 初始化AHT20
bool aht20_start_measurement(void);  // 开始AHT20的测量
bool aht20_wait_for_measurement(void);  // 等待AHT20的测量完成
bool aht20_read_measurement(float *temperature, float *humidity);  // 读取AHT20的测量结果

#endif  // __AHT20_H__
