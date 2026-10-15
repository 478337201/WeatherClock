#ifndef __BOARD_H__
#define __BOARD_H__

#define IS_DEBUG_VERSION    0  // 是否是调试版本

void board_lowlevel_init(void);  // 初始化外设时钟
void board_init(void);  // 初始化片上外设
void per_sleep_processing(void);  
void post_sleep_processing(void);  

#endif  // __BOARD_H__
