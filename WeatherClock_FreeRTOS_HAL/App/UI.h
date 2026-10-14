/**
 * @file    UI.h
 * @brief   显示绘制层对外接口（RGB565 颜色与绘图入口）
 */
#ifndef __UI_H
#define __UI_H

#include <stdint.h>
#include "image.h"
#include "font.h"

#define UI_WIDTH  240   /* 屏幕宽度（像素） */
#define UI_HEIGHT 320   /* 屏幕高度（像素） */

/* RGB888 -> RGB565 颜色转换 */
#define mkcolor(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
#define     RED          mkcolor(255,0,0)	  /* 红色 */
#define     GREEN        mkcolor(0,255,0)	  /* 绿色 */
#define     BLUE         mkcolor(0,0,255)	  /* 蓝色 */
#define     WHITE        mkcolor(255,255,255)	  /* 白色 */
#define     BLACK        mkcolor(0,0,0)  /* 黑色 */


void UI_Init(void);                        /* 创建 UI 消息队列与 UI 任务 */
void UI_print_color(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2,uint16_t colours);   /* 填充指定矩形区域 */
void UI_write_string(uint16_t x,uint16_t y,const char *str,uint16_t colour,uint16_t bg_colour,const font_t *font);   /* 绘制字符串 */
void UI_print_image(uint16_t x,uint16_t y,const image_t *image);   /* 绘制图片 */


#endif
