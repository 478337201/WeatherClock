#ifndef __CONSOLE_H__
#define __CONSOLE_H__

#include <stdint.h>

void console_init(void);  // 初始化串口
void console_write(const char *data, uint32_t len);
void console_EasyLogger_Init(void);

#endif  // __CONSOLE_H__
