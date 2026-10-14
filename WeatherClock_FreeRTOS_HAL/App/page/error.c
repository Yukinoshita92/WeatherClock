/**
 * @file    error.c
 * @brief   错误提示页：显示错误插图与居中的提示文字
 */
#include <stdint.h>
#include <string.h>
#include "UI.h"

/* 绘制错误页：整屏白底 + 错误插图 + 居中文字 */
void error_page_display(const char* msg)
{
	UI_print_color(0,0,UI_WIDTH-1,UI_HEIGHT-1,WHITE);
	UI_print_image(20,4,&Image_error);
	/* 依据文本长度计算居中起始横坐标 */
	uint16_t startx=0;
	uint16_t len=strlen(msg)*font20_sbold_t.fSize/2;   /* 英文字符占半个像素点 */
	if(len<UI_WIDTH)
	{
		startx=(UI_WIDTH-len+1)/2;   /* +1 用于字符数为奇数时仍能居中 */
	}
	UI_write_string(startx,200,msg,RED,WHITE,&font20_sbold_t);
}
