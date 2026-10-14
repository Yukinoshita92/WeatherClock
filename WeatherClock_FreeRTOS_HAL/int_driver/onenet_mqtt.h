/**
 * @file    onenet_mqtt.h
 * @brief   OneNET 云平台 MQTT 接入接口（App 端）
 * @details 完成设备接入（配置鉴权三要素 -> 连接 broker -> 订阅物模型服务调用主题），
 *          并向 OTA 层提供"等待并解析升级通知"的能力。
 * @note    注意事项：
 *            - 必须在 WiFi 连接成功之后调用初始化；
 *            - 升级通知的业务字段位于报文 params 段内；
 *            - 下载固件前需 disconnect、下载后 reconnect，避免与透传抢串口。
 */
#ifndef __ONENET_MQTT_H__
#define __ONENET_MQTT_H__

#include <stdint.h>
#include <stdbool.h>

/* ================= 升级通知结构 ================= */
typedef struct
{
    char version[32];   /* 目标固件版本号，如 "v1.3.0" */
    char url[256];      /* 固件下载 URL */
} onenet_ota_notify_t;

/**
 * @brief  初始化并接入 OneNET（配置 + 连接 + 订阅升级主题）。
 * @return true=成功；false=失败（失败不阻塞，可稍后调用 reconnect 重试）。
 * @note   必须在 WiFi 连接成功之后调用。
 */
bool onenet_mqtt_init(void);

/**
 * @brief  断开 MQTT 连接（下载固件前调用，让出 USART2）。
 */
void onenet_mqtt_disconnect(void);

/**
 * @brief  重连 MQTT 并重新订阅（下载结束后 / 断线恢复时调用）。
 * @return true=成功；false=失败。
 */
bool onenet_mqtt_reconnect(void);

/**
 * @brief  阻塞等待升级通知（带超时，事件驱动）。
 * @param  out        输出：解析出的版本号与下载 URL。
 * @param  timeout_ms 超时（毫秒）。
 * @return true=收到有效通知；false=超时或无有效通知。
 */
bool onenet_mqtt_wait_notify(onenet_ota_notify_t *out, uint32_t timeout_ms);

/**
 * @brief  查询 MQTT 连接状态。
 * @return true=已连接；false=未连接/已断开。
 */
bool onenet_mqtt_is_connected(void);

#endif /* __ONENET_MQTT_H__ */
