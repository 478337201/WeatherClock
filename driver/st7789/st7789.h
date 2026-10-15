#ifndef __ST7789_H__
#define __ST7789_H__

#include <stdint.h>

#define ST7789_WIDTH    240  // LCD屏幕宽度
#define ST7789_HEIGHT   320  // LCD屏幕高度

void st7789_init(void);  // 初始化ST7789
void st7789_fill_color(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);  // 填充颜色
void st7789_write_pixels(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const uint8_t *pixels, uint32_t length);  // 刷新窗口

#endif  // __ST7789_H__
