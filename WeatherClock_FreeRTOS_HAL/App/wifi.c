/**
 * @file    wifi.c
 * @brief   Wi-Fi 上电初始化流程：ESP-AT 初始化 -> 连接 AP -> SNTP 校时
 * @note    本模块日志 TAG = "WIFI"
 */
#include "esp_at.h"
#include "page.h"
#include "FreeRTOS.h"
#include "task.h"

/* 分级日志框架：本模块 TAG=WIFI */
#define LOG_TAG "WIFI"
#include "log.h"

#include "app_config.h"      /* 配置收口：WiFi 凭据统一由 app_config.h 提供 */

/* 上电初始化：依次完成 ESP-AT 初始化、连接 AP、配置 SNTP 时区；任一环节失败即进入错误页并停止运行 */
void wifi_init(void)
{
		LOG_I("startup\n");                                        /* 上电启动 */
    
    if (!esp_at_Init())
    {
        LOG_E("AT init failed\n");
        goto err;
    }
    LOG_I("AT inited\n");
    
    if (!esp_at_wifi_init())
    {
        LOG_E("Wifi init failed\n");
        goto err;
    }
    LOG_I("WIFI inited\n");
	
	if (!esp_at_sntp_init())
    {
        LOG_E("SNTP init failed\n");
        goto err;
    }
    LOG_I("SNTP inited\n");
	
    return;
	
err:
	/* 初始化失败：进入错误页并停留，不再返回调用方 */
	error_page_display("[WIFI] Init Fail!!");
	
	while(1)
	{
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}	
/* 上电连接 AP：以 10s 为自定义超时轮询连接状态，超时后进入错误页 */
void wifi_wait_connect(void)
{
	LOG_I("connecting\n");
	esp_at_connect_wifi(CFG_WIFI_SSID, CFG_WIFI_PWD, CFG_WIFI_BSSID); 
	/* 每 100ms 查询一次连接状态，累计 10s 未连上则判定超时 */
	for(uint32_t t=0;t<10*1000;t+=100)
	{
		vTaskDelay(pdMS_TO_TICKS(100));
		wifi_info_t wifi = { 0 };                          /* 本次查询到的 Wi-Fi 信息 */
		if (esp_at_get_wifi_info(&wifi) && wifi.connected)
		{
			LOG_I("connected\r\n");
			LOG_I("SSID: %s, BSSID: %s, Channel: %d, RSSI: %d\n",
					wifi.ssid, wifi.bssid, wifi.channel, wifi.rssi);
			return;
		}
	}   
	/* 连接超时：进入错误页循环停留 */
	LOG_E("connect Timeout!!");
	error_page_display("[Wifi] Connect Timeout!!\r\n");
	while(1)
	{
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
