/**
 * @file    esp_at.h
 * @brief   ESP-AT 模组驱动接口：AT 指令收发、Wi-Fi/HTTP/SNTP 与天气解析
 */
#ifndef __ESP_AT_H
#define __ESP_AT_H


#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

/* Wi-Fi 连接信息 */
typedef struct WIFI
{
	char ssid[64];
	char pwd[64];
	char bssid[64];
	int rssi;
	int channel;
	bool connected;
}wifi_info_t;

/* 从心知天气取回的返回值中，封装有用的信息 */
typedef struct WEATHER
{
	char City[32];      /* 城市名称 */
	char Path[128];     /* 城市地址 */
	char Weather[16];   /* 天气信息 */
	int Code;           /* 天气代码 */
	float Temperature;  /* 温度 */
}weather_info_t;

/* SNTP 日期时间 */
typedef struct TIME
{
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint8_t weekday;
}date_info_t;

bool esp_at_Init(void);                                              /* 模组初始化 */
const char* esp_at_get_response(void);                               /* 获取最近一次 AT 响应 */
bool esp_at_send_command(const char* command, uint64_t timeout_ms);  /* 发送 AT 指令并等待响应 */
/* OTA 下载流程进行中为 true，用于禁止天气等其他任务发送 AT 指令 */
extern bool g_ota_busy;
/* 内部发送接口：不检查 g_ota_busy / g_in_passthrough，供 OTA 模块在下载流程内使用 */
bool esp_at_send_command_internal(const char* command, uint64_t timeout_ms);

bool esp_at_connect_wifi(char* ssid ,char *pwd ,char* bssid);        /* 连接指定 AP */
bool esp_at_wifi_init(void);                                         /* Wi-Fi 工作模式初始化 */
bool esp_at_get_wifi_info(wifi_info_t *info);                        /* 查询当前 Wi-Fi 信息 */

bool seniverse_parse(const char* response, weather_info_t* info);    /* 解析心知天气响应报文 */
const char* esp_at_get_http(const char*url);                         /* 执行 HTTP GET 并返回响应体 */

bool esp_at_sntp_init(void);                                         /* SNTP 时区初始化 */
bool esp_at_get_time(date_info_t* info);                             /* 获取网络时间 */

#endif
