/**
 * @file    app_config.h
 * @brief   全局配置收口：WiFi / 天气 / OneNET / 固件托管
 * @note    公开仓库中本文件只含占位值，请勿提交真实凭据。
 *          本地启用真实值见文件末尾「5. 本地真实凭据」说明。
 */
#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

/* ---------- 1. WiFi 接入点 ---------- */
#ifndef CFG_WIFI_SSID
#define CFG_WIFI_SSID   "YOUR_WIFI_SSID"
#endif
#ifndef CFG_WIFI_PWD
#define CFG_WIFI_PWD    "YOUR_WIFI_PASSWORD"
#endif
#define CFG_WIFI_BSSID  NULL          /* NULL = 由 ESP 模块自动选择 AP */

/* ---------- 2. 心知天气（Seniverse） ---------- */
#ifndef CFG_WEATHER_KEY
#define CFG_WEATHER_KEY "YOUR_SENIVERSE_KEY"
#endif
#ifndef CFG_WEATHER_LOC
#define CFG_WEATHER_LOC "YOUR_LOCATION_ID"
#endif

/* ---------- 3. OneNET 新版 Studio（MQTT） ---------- */
#ifndef CFG_ONENET_PID
#define CFG_ONENET_PID    "YOUR_PRODUCT_ID"
#endif
#ifndef CFG_ONENET_DEV
#define CFG_ONENET_DEV    "YOUR_DEVICE_NAME"
#endif
#ifndef CFG_ONENET_TOKEN
#define CFG_ONENET_TOKEN  "YOUR_ONENET_TOKEN"
#endif
#define CFG_ONENET_HOST   "mqtts.heclouds.com"   /* 官方文档 MQTT 3.1.1 非加密 */
#define CFG_ONENET_PORT   1883
#define CFG_ONENET_SERVICE_ID  "ota_upgrade"

/* ---------- 4. 固件托管（对象存储） ---------- */
#ifndef CFG_OTA_HOST
#define CFG_OTA_HOST "YOUR_BUCKET.cos.YOUR_REGION.myqcloud.com"
#endif
#define CFG_OTA_PORT 80
#define CFG_OTA_PATH "/app_upgrade.xbin"


/* #include "app_config_local.h" */

#endif /* __APP_CONFIG_H */
