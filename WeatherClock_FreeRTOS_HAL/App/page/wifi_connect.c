/**
 * @file    wifi_connect.c
 * @brief   Wi-Fi 连接页：显示 Wi-Fi 插图、两行提示文字与loading动画帧
 */
#include <stdint.h>
#include <string.h>
#include "UI.h"

/* 依据文本长度计算居中起始横坐标 */
static uint16_t get_startx(const char* str)
{
	uint16_t len=strlen(str)*font16_sbold_t.fSize/2;   /* 英文字符占半个像素点 */
	if(len<UI_WIDTH)
	{
		return ((UI_WIDTH-len+1)/2);   /* +1 用于字符数为奇数时仍能居中 */
	}
	else{return 0;}
}

/* 绘制 Wi-Fi 连接页：两行提示文字分别居中显示 */
void wifi_page_display(const char* msg1,const char* msg2)
{
	UI_print_color(0,0,UI_WIDTH-1,UI_HEIGHT-1,BLACK);
	UI_print_image(30,8,&Image_wifi);
	
	uint16_t startx=0;
	startx=get_startx(msg1);
	UI_write_string(startx,210,msg1,WHITE,BLACK,&font20_sbold_t);
	
	startx=get_startx(msg2);
	UI_write_string(startx,245,msg2,WHITE,BLACK,&font20_sbold_t);
	
	UI_print_image(106,280,&Image_reloading);
}
