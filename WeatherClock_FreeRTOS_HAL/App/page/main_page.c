/**
 * @file    main_page.c
 * @brief   主页面：整体布局绘制与各信息区域的局部重绘接口
 * @note    所有绘制均通过 UI 层接口入队，本层不做耗时操作
 */
#include <stdint.h>
#include "UI.h"
#include "page.h"

#define info_bgcolor    mkcolor(61,198,251)    /* 顶部信息栏背景色 */
#define indoor_bgcolor  mkcolor(200,255,137)   /* 室内区域背景色 */
#define outdoor_bgcolor WHITE                  /* 室外区域背景色 */

/* 绘制主页面整体框架，并填充各区域的占位内容 */
void main_page_display(void)
{
	UI_print_color(0,0,UI_WIDTH-1,UI_HEIGHT-1,BLACK);
	/* 三个信息区域的底板 */
	UI_print_color(8,8,8+224-1,8+130-1,info_bgcolor);
	UI_print_color(8,146,8+108-1,146+166-1,indoor_bgcolor);
	UI_print_color(124,146,124+108-1,146+166-1,outdoor_bgcolor);
	
	/* 各区域先填充占位内容，等待后续定时刷新覆盖 */
	
	/* 主页面信息 */
	do
	{
		main_page_redraw_wifi_ssid("Unkown Wifi");
		UI_write_string(204,12,"100%",WHITE,info_bgcolor,&font12_sbold_t);
		UI_write_string(25,30,"24:60",BLACK,info_bgcolor,&font76_bold_t);
		UI_write_string(22,110,"1000/13/32",BLACK,info_bgcolor,&font16_sbold_t);
		UI_write_string(160,110,"----",BLACK,info_bgcolor,&font16_sbold_t);
		UI_print_image(167,10,&Image_5g);
		UI_print_image(185,10,&Image_charge);
		UI_print_image(20,10,&Image_wifi2);
	}while(0);

	/* 室内温度 */
	do
	{
		UI_write_string(14,150,"室内环境",BLACK,indoor_bgcolor,&font24_bold_t);
		 main_page_redraw_indoor_temperature(999.9f);
        main_page_redraw_indoor_humidity(999.9f);
		UI_write_string(92,255,"%",BLACK,indoor_bgcolor,&font24_bold_t);
		UI_write_string(84,177,"C",BLACK,indoor_bgcolor,&font24_bold_t);
		
	}while(0);
	/* 室外温度 */
	do
	{
		main_page_redraw_outdoor_city("南山区");
        main_page_redraw_outdoor_temperature(999.9f);
        main_page_redraw_outdoor_weather_icon(-1);
		UI_print_image(129,246,&Image_temperature);
		UI_write_string(197,179,"C",BLACK,outdoor_bgcolor,&font24_bold_t);
	}while(0);
}

/* 重绘顶部信息栏的 Wi-Fi 名称（固定 18 字符宽度） */
void main_page_redraw_wifi_ssid(const char *ssid)
{
    char str[19];
    snprintf(str, sizeof(str), "%18s", ssid);
    UI_write_string(46, 12, str, WHITE, info_bgcolor, &font12_sbold_t);
}

/* 重绘时间：秒为偶数时冒号点亮，奇数时冒号位置为空，形成闪烁效果 */
void main_page_redraw_time(rtc_date_t *time)
{
    char str[6];
    char comma = (time->second % 2 == 0) ? ':' : ' ';
    snprintf(str, sizeof(str), "%02u%c%02u", time->hour, comma, time->minute);   /* 右对齐，占满 5 字符宽度 */
    UI_write_string(25, 30, str, BLACK, info_bgcolor, &font76_bold_t);
}

/* 重绘日期与星期 */
void main_page_redraw_date(rtc_date_t *date)
{
    char str1[11];
	char str2[10];

    snprintf(str1, sizeof(str1), "%04u/%02u/%02u", date->year, date->month, date->day);
	snprintf(str2, sizeof(str2), "星期%s", 
        date->weekday == 1 ? "一" :
        date->weekday == 2 ? "二" :
        date->weekday == 3 ? "三" :
        date->weekday == 4 ? "四" :
        date->weekday == 5 ? "五" :
        date->weekday == 6 ? "六" :
        date->weekday == 7 ? "日" : "X");   /* 自定义星期显示 */
	UI_write_string(22,110,str1,BLACK,info_bgcolor,&font16_sbold_t);
	UI_write_string(160,110,str2,BLACK,info_bgcolor,&font16_sbold_t);
}

/* 重绘室内温度，超出可显示范围时保持 "--" */
void main_page_redraw_indoor_temperature(float temperature)
{
    char str[3] = {'-', '-'};                       /* 占位显示 "--" */
    if (temperature > -10.0f && temperature <= 100.0f)   /* 仅 -9~99 ℃ 可正常显示 */
        snprintf(str, sizeof(str), "%2.0f", temperature);   /* 右对齐，取整显示 */
	UI_write_string(33,179,str,BLACK,indoor_bgcolor,&font50_sbold_t);
}
    
/* 重绘室内湿度，超出可显示范围时保持 "--" */
void main_page_redraw_indoor_humidity(float humidity)
{
    char str[3]={'-','-'};
    if (humidity > 0.0f && humidity <= 99.99f)   /* 仅 0%~99.9% 可正常显示 */
        snprintf(str, sizeof(str), "%2.0f", humidity);   /* 右对齐，取整显示 */
	UI_write_string(27,232,str,BLACK,indoor_bgcolor,&font60_extra_t);
}

/* 重绘室外城市名 */
void main_page_redraw_outdoor_city(const char *city)
{
    char str[9];
    snprintf(str, sizeof(str), "%s", city);
	UI_write_string(130,151,city,BLACK,outdoor_bgcolor,&font24_bold_t);
}

/* 重绘室外温度，超出可显示范围时保持 "--" */
void main_page_redraw_outdoor_temperature(float temperature)
{
    char str[3] = {'-', '-'};
    if (temperature > -10.0f && temperature <= 100.0f)
        snprintf(str, sizeof(str), "%2.0f", temperature);
    UI_write_string(144,179,str,BLACK,outdoor_bgcolor,&font50_sbold_t);
}

/* 按心知天气的天气代码选择对应图标并绘制 */
void main_page_redraw_outdoor_weather_icon(const int code)
{
    const image_t *icon;
    if (code == 0 || code == 2 || code == 38)
        icon = &Image_sun;
    else if (code == 1 || code == 3)
        icon = &Image_moon;
    else if (code == 9)
        icon = &Image_yin;
    else if (code == 4 || code == 5 || code == 6 || code == 7 || code == 8)
        icon = &Image_crowd;
    else if (code == 10 || code == 13 || code == 14 || code == 15 || code == 16 || code == 17 || code == 18 || code == 19)
        icon = &Image_rain;
    else if (code == 11 || code == 12)
        icon = &Image_thunder;   /* 雷阵雨 */
    else if (code == 20 || code == 21 || code == 22 || code == 23 || code == 24 || code == 25)
        icon = &Image_yuki;
    else /* 扬沙、龙卷风等未收录的天气，统一使用 NA 图标 */
        icon = &Image_na;
    UI_print_image(173,240,icon);
}
