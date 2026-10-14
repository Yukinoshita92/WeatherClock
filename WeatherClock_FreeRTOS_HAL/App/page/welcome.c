/**
 * @file    welcome.c
 * @brief   开机欢迎页：显示品牌图片与标语
 */
#include <stdint.h>
#include "UI.h"

/* 绘制欢迎页 */
void welcome_page_display(void)
{
	UI_print_color(0,0,UI_WIDTH-1,UI_HEIGHT-1,BLACK);
	UI_print_image(30,12,&Image_main);
	UI_write_string(30,200,"WeatherClock",mkcolor(93,222,244),BLACK,&font32_sbold_t);
	UI_write_string(59,234,"天气时钟",mkcolor(201,234,166),BLACK,&font32_sbold_t);
	UI_write_string(52,274,"心晴胜于天晴",mkcolor(154,215,202),BLACK,&font24_sbold_t);
}
