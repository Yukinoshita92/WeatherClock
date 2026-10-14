/**
 * @file    st7789.h
 * @brief   ST7789 SPI 液晶屏驱动接口
 */
#ifndef __ST7789_H
#define __ST7789_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "font.h"
#include "image.h"

#define LCD_WIDTH  240   /* 屏幕宽度（像素） */
#define LCD_HEIGHT 320   /* 屏幕高度（像素） */

/* RGB888 -> RGB565 颜色转换 */
#define mkcolor(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

#define     RED          mkcolor(255,0,0)	  /* 红色 */
#define     GREEN        mkcolor(0,255,0)	  /* 绿色 */
#define     BLUE         mkcolor(0,0,255)	  /* 蓝色 */
#define     WHITE        mkcolor(255,255,255)	  /* 白色 */
#define     BLACK        mkcolor(0,0,0)  /* 黑色 */

void st7789_init(void);                                                                    /* 屏幕初始化 */
bool st7789_print_color(uint16_t x1,uint16_t y1, uint16_t x2,uint16_t y2,uint16_t color);   /* 区域填色 */
bool st7789_write_string(uint16_t x,uint16_t y,const char *str,uint16_t colour,uint16_t bg_colour,const font_t *font);   /* 绘制字符串 */
bool st7789_print_image(uint16_t x,uint16_t y,const image_t *image);                        /* 绘制图片 */

#endif
