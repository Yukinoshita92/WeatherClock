/**
 * @file    esp_at_mqtt.h
 * @brief   MQTT AT 指令封装与事件接收接口
 * @details 指令走 AT 命令模式，复用 esp_at.c 的发送接口；ESP 上报的 MQTT 事件由
 *          esp_at.c 的串口中断按行转交本模块缓存，并以信号量通知上层任务。
 * @note    注意事项：
 *            - 与透传下载互斥，二者不会同时占用 USART2；
 *            - payload 为"单条 + 覆盖"语义，只保留最新一条；
 *            - 事件处理在中断上下文执行，只做缓存、不做解析。
 */
#ifndef __ESP_AT_MQTT_H__
#define __ESP_AT_MQTT_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief  初始化（创建事件信号量），首次使用前调用一次（可重复调用）。
 */
void esp_at_mqtt_init(void);

/* ================= MQTT AT 指令封装 ================= */

/**
 * @brief  配置 MQTT 用户参数（AT+MQTTUSERCFG）。
 * @param  client_id  客户端 ID（OneNET Studio 为设备名，以控制台为准）。
 * @param  username   用户名（产品 ID）。
 * @param  password   密码（鉴权 token）。
 * @return true=成功；false=失败。
 */
bool esp_at_mqtt_user_cfg(const char *client_id, const char *username, const char *password);

/**
 * @brief  连接 MQTT 服务器（AT+MQTTCONN）。
 * @param  host  broker 域名/IP。
 * @param  port  broker 端口。
 * @return true=命令成功；false=失败。
 */
bool esp_at_mqtt_connect(const char *host, uint16_t port);

/**
 * @brief  订阅主题（AT+MQTTSUB）。
 * @param  topic  主题字符串。
 * @param  qos    QoS 等级（0/1/2）。
 * @return true=成功；false=失败。
 */
bool esp_at_mqtt_subscribe(const char *topic, int qos);

/**
 * @brief  发布消息（AT+MQTTPUB）。
 * @param  topic    主题字符串。
 * @param  payload  消息体（字符串）。
 * @param  qos      QoS 等级（0/1/2）。
 * @return true=成功；false=失败。
 */
bool esp_at_mqtt_publish(const char *topic, const char *payload, int qos);

/**
 * @brief  断开 MQTT 连接（AT+MQTTCLEAN）。
 * @return true=成功；false=失败。
 */
bool esp_at_mqtt_clean(void);

/* ================= 事件接收（仅 esp_at.c 接收中断调用） ================= */

/**
 * @brief  处理一行 "+MQTTSUBRECV:..." 事件。
 * @note   中断上下文执行，仅做轻量缓存 + 给信号量，不解析。
 */
void esp_at_mqtt_on_subrecv(const char *line);

/**
 * @brief  处理连接状态事件。
 * @param  connected  true=已连接（+MQTTCONNECTED）；false=已断开（+MQTTDISCONNECTED）。
 * @note   中断上下文执行，仅置标志。
 */
void esp_at_mqtt_on_conn_evt(bool connected);

/* ================= 上层接口（任务上下文调用） ================= */

/**
 * @brief  等待 MQTT 事件信号量（带超时）。
 * @param  timeout_ms  超时（毫秒）。
 * @return true=有事件到达；false=超时。
 */
bool esp_at_mqtt_wait_event(uint32_t timeout_ms);

/**
 * @brief  取走最新一条订阅消息的 JSON payload。
 * @param  out  输出缓冲区。
 * @param  len  输出缓冲区大小（必须 > 0）。
 * @return true=有新消息并已拷贝；false=无新消息或参数非法。
 */
bool esp_at_mqtt_poll_payload(char *out, uint32_t len);

/**
 * @brief  查询 MQTT 连接状态。
 * @return true=已连接；false=未连接/已断开。
 */
bool esp_at_mqtt_is_connected(void);

#endif /* __ESP_AT_MQTT_H__ */
