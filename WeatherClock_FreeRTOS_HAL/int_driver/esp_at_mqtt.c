/**
 * @file    esp_at_mqtt.c
 * @brief   MQTT AT 指令封装与事件接收
 * @details 对 AT 固件的 MQTT 指令（USERCFG / CONN / SUB / PUB / CLEAN）做一层封装，
 *          并承接 ESP 主动上报的事件：+MQTTSUBRECV 写入消息缓存并释放信号量，
 *          +MQTTCONNECTED / +MQTTDISCONNECTED 更新连接状态。
 *          上层等待事件用 esp_at_mqtt_wait_event()，取消息用 esp_at_mqtt_poll_payload()。
 * @note    注意事项：
 *            - 事件在串口中断上下文写入，仅做轻量缓存，解析交给上层任务；
 *            - 消息缓存为"单条 + 覆盖"语义，只保留最新一条，最长 511 字节；
 *            - 断线重连由上层（onenet_mqtt）负责，本文件只维护连接状态。
 */

#include "esp_at_mqtt.h"
#include "esp_at.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <string.h>
#include <stdio.h>

/* 分级日志框架：本模块 TAG=MQTT */
#define LOG_TAG "MQTT"
#include "log.h"

/* ================= MQTT 事件信号量 ================= */
static SemaphoreHandle_t s_event_sem = NULL;

/* ================= 订阅消息缓存（单条 + 覆盖语义） ================= */
/* 中断与任务共享，需 volatile；单条消息最长 512 字节 */
static volatile char s_payload[512];
static volatile bool s_payload_ready = false;

/* ================= 连接状态 ================= */
static volatile bool s_connected = false;

/**
 * @brief  初始化（创建事件信号量），首次使用前调用一次。
 */
void esp_at_mqtt_init(void)
{
    if (s_event_sem == NULL)
    {
        s_event_sem = xSemaphoreCreateBinary();
        configASSERT(s_event_sem);
    }
}

/**
 * @brief  配置 MQTT 用户参数（AT+MQTTUSERCFG）。
 * 格式：AT+MQTTUSERCFG=0,1,"<client_id>","<username>","<password>",0,0,""
 * 0：linkID ESPC3 只支持ID0
 * 1：scheme：方案 1使用TCP链接
 * client_id：MQTT客户端ID （当前指的是onenet新建的设备号）
 * username：用户名
 * password：密码
 * 0：证书ID：只支持0
 * 0：CA ID：只支持0
 * ""：Cipheri
 */
bool esp_at_mqtt_user_cfg(const char *client_id, const char *username, const char *password)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\r\n",
             client_id, username, password);
    return esp_at_send_command(cmd, 5000u);
}

/**
 * @brief  连接 MQTT broker（AT+MQTTCONN）。
 * 连接 OneNET 官方规定的 MQTT broker 域名地址
 * 格式：AT+MQTTCONN=0,"<host>",<port>,1   （最后 1 表示允许自动重连）
 * 0：linkID ESPC3 只支持ID0
 * host：MQTT服务器域名或IP地址（当前是存放固件的域名地址（腾讯云cos域名））
 * port：MQTT服务器端口
 * reconnect：
 *   0: MQTT 不自动重连。如果 MQTT 建立连接后又断开，则无法再次使用本命令重新建立连接，
 *      需要先发送 AT+MQTTCLEAN=0 命令清理信息，重新配置参数，再建立新的连接。
 *   1：MQTT 自动重连，会消耗较多的内存资源。
 */
bool esp_at_mqtt_connect(const char *host, uint16_t port)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+MQTTCONN=0,\"%s\",%u,1\r\n", host, (unsigned)port);
    return esp_at_send_command(cmd, 10000u);
}

/**
 * @brief  订阅主题（AT+MQTTSUB）。
 * 格式：AT+MQTTSUB=0,"<topic>",<qos>
 * 0：linkID ESPC3 只支持ID0
 * topic：MQTT主题（当前是使用onenet物模型建立的ota_upgrade主题 实际的主题订阅经过onenet平台规定下的MQTT处理有对应的主题格式）
 * qos：MQTT服务质量等级（0、1、2）
 */
bool esp_at_mqtt_subscribe(const char *topic, int qos)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=0,\"%s\",%d\r\n", topic, qos);
    return esp_at_send_command(cmd, 10000u);
}

/**
 * @brief  发布消息（AT+MQTTPUB）。
 * 格式：AT+MQTTPUB=0,"<topic>","<payload>",<qos>,0
 * 0：linkID ESPC3 只支持ID0
 * topic：MQTT主题
 * payload：MQTT负载（消息内容）
 * qos：MQTT服务质量等级（0、1、2）
 * 0：retain（0：不保留，1：保留） 1会将最新一条的消息保留 新订阅的设备会收到这条消息
 */
bool esp_at_mqtt_publish(const char *topic, const char *payload, int qos)
{
    char cmd[640];
    snprintf(cmd, sizeof(cmd), "AT+MQTTPUB=0,\"%s\",\"%s\",%d,0\r\n", topic, payload, qos);
    return esp_at_send_command(cmd, 5000u);
}

/**
 * @brief  断开 MQTT 连接（AT+MQTTCLEAN）。
 * 0：linkID 只支持0
 */
bool esp_at_mqtt_clean(void)
{
    return esp_at_send_command("AT+MQTTCLEAN=0\r\n", 5000u);
}

/**
 * @brief  接收一行 "+MQTTSUBRECV:..." 响应（仅在 esp_at.c 接收中断调用 轻量处理 不解析）。
 */
void esp_at_mqtt_on_subrecv(const char *line)
{
    const char *json = strchr(line, '{');/* 定位到 data 消息负载的开头 */
    uint32_t     i;
    BaseType_t   woken = pdFALSE;/* 使用信号量需要记录是否有更高优先级任务被唤醒 */

    if (json == NULL)
    {
        /* 非 JSON 通知（或格式异常），忽略 */
        return;
    }

    /* 缓存消息负载，按"覆盖"语义写入：升级通知只关心最新一条 */
    for (i = 0; json[i] != '\0' && i < sizeof(s_payload) - 1; i++)
    {
        s_payload[i] = json[i];
    }
    s_payload[i] = '\0';
    s_payload_ready = true;

    /* 释放信号量，以事件方式通知上层有新消息 */
    if (s_event_sem != NULL)
    {
        xSemaphoreGiveFromISR(s_event_sem, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

/**
 * @brief  说明当前连接状态（仅 esp_at.c 接收中断调用）。
 */
void esp_at_mqtt_on_conn_evt(bool connected)
{
    s_connected = connected;
}

/*===========================上层接口 外部调用=================================*/
/**
 * @brief  查询 MQTT 连接状态。
 */
bool esp_at_mqtt_is_connected(void)
{
    return s_connected;
}

/**
 * @brief  等待 MQTT 事件信号量（带超时）。
 */
bool esp_at_mqtt_wait_event(uint32_t timeout_ms)
{
    if (s_event_sem == NULL)
    {
        return false;
    }/* 信号量还没初始化 */
    return (xSemaphoreTake(s_event_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
    /* 二进制信号量：返回 pdTRUE 表示已被释放，说明有新消息可取 */
}

/**
 * @brief  取走最新一条订阅消息的 JSON payload（任务上下文）。
 */
bool esp_at_mqtt_poll_payload(char *out, uint32_t len)
{
    uint32_t i;

    /* 参数防护：len 为 0 时 "len - 1" 会无符号下溢，导致越界写 */
    if (out == NULL || len == 0)
    {
        return false;
    }
    /*
     当前是典型的单生产者+单消费者模式 使用临界区保护 
     */ 
    taskENTER_CRITICAL();
    if (!s_payload_ready)
    {
        taskEXIT_CRITICAL();/* 退出临界区 */
        return false;
    }

    /* 临界区内拷贝，避免与中断写入并发产生数据撕裂 */
    for (i = 0; i < len - 1 && s_payload[i] != '\0'; i++)
    {
        out[i] = s_payload[i];
    }
    out[i] = '\0';
    s_payload_ready = false;
    taskEXIT_CRITICAL();

    return true;
}
