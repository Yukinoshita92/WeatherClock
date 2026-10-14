/**
 * @file    page.h
 * @brief   页面显示接口：整页切换与主页面局部重绘
 */
#ifndef __PAGE_H
#define __PAGE_H

#include <stdint.h>
#include "rtc.h"

/* 整页显示 */
void error_page_display(const char* msg);                    /* 错误提示页 */
void welcome_page_display(void);                             /* 欢迎页 */
void wifi_page_display(const char* msg1,const char* msg2);   /* Wi-Fi 连接页 */
void main_page_display(void);                                /* 主页面 */

/* 主页面局部重绘 */
void main_page_redraw_wifi_ssid(const char *ssid);
void main_page_redraw_time(rtc_date_t *time);
void main_page_redraw_date(rtc_date_t *date);
void main_page_redraw_indoor_temperature(float temperature);
void main_page_redraw_indoor_humidity(float humidity);
void main_page_redraw_outdoor_city(const char *city);
void main_page_redraw_outdoor_temperature(float temperature);
void main_page_redraw_outdoor_weather_icon(const int code);

#endif /*__PAGE__H*/
