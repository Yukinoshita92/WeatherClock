/**
 * @file    onenet_mqtt.c
 * @brief   OneNET 云平台 MQTT 接入与升级通知解析（App 端）
 * @details 基于 esp_at_mqtt 完成设备接入：配置鉴权三要素 -> 连接 broker -> 订阅物模型
 *          服务调用主题；订阅消息到达后解析出其中的版本号与固件 URL，供 OTA 任务消费。
 *          对外接口：
 *            - onenet_mqtt_init()          接入（失败不阻塞，可由 HTTP 兜底）
 *            - onenet_mqtt_wait_notify()   阻塞等待并解析一条升级通知
 *            - onenet_mqtt_disconnect() / onenet_mqtt_reconnect()  下载前断开、下载后重连
 * @note    注意事项：
 *            - 鉴权三要素与 broker 地址定义在文件顶部，请按实际设备替换；
 *            - 业务字段位于报文的 params 段内，解析时优先在 params 内查找；
 *            - 本模块与透传下载互斥，不可同时占用 USART2。
 */

#include "onenet_mqtt.h"
#include "esp_at_mqtt.h"
#include "main.h"               /* HAL_GetTick */
#include <string.h>
#include <stdio.h>

/* 分级日志框架：本模块 TAG=ONENET */
#define LOG_TAG "ONENET"
#include "log.h"
#include "app_config.h"      /* 配置收口：鉴权三要素等统一提供 */

/* ================= OneNET 鉴权配置（值见 App/app_config.h） ================= */
#define ONENET_PRODUCT_ID   CFG_ONENET_PID    /* 产品 ID */
#define ONENET_DEVICE_NAME  CFG_ONENET_DEV    /* 设备名称（新版 Studio 作为 clientId） */
#define ONENET_TOKEN        CFG_ONENET_TOKEN  /* 预生成鉴权 token */

/* ================= OneNET broker 配置（以官方文档/控制台为准） ================= */
#define ONENET_MQTT_HOST    CFG_ONENET_HOST    /* OneNET MQTT broker（官方文档：MQTT 3.1.1 非加密） */
#define ONENET_MQTT_PORT    CFG_ONENET_PORT    /* 非加密端口（TLS 用 8883，域名 mqttstls.heclouds.com） */

/* ================= 升级通知主题（物模型服务调用） ================= */
/* 服务 ID 取自 OneNET 物模型，参数包含 version 与 url */
#define ONENET_OTA_SERVICE_ID   CFG_ONENET_SERVICE_ID
#define ONENET_SUB_TOPIC        "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME \
                                "/thing/service/" ONENET_OTA_SERVICE_ID "/invoke"

/* ================= 事件等待步长 ================= */
#define ONENET_POLL_STEP_MS 50u    /* wait_notify 内部单次等待步长 */

/*=============================MQTT消息解析===================================*/
/**
 * @brief  从 JSON 字符串(payload)中解析需要的字段内容（优先在 params 段内查找）。
 * @param  json     JSON 原文。
 * @param  key      字段名（不含引号）。
 * @param  out      输出缓冲区。
 * @param  out_len  输出缓冲区大小。
 * @return true=提取成功；false=未找到该字段。
 *   物模型服务调用报文的业务字段位于 "params" 内，先定位 "params": 再查找解析；
 *   若无 params字段区分 则整段查找。
 */
static bool json_extract_str(const char *json, const char *key, char *out, uint32_t out_len)
{
    char        pattern[64];
    const char *search;
    const char *p;
    const char *q;
    uint32_t    len;

    /* 优先在 params 段内查找 */
    search = strstr(json, "\"params\":");
    if (search == NULL)
    {
        search = json;   /* 无 params，整段查找 */
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);/* 拼出字段前缀，形如 "version":" */

    p = strstr(search, pattern);/* 定位字段，其后即为待解析的内容 */
    if (p == NULL)/* 没有对应的字段 */
    {
        return false;
    }
    p += strlen(pattern);/* 跳过字段前缀，指向内容起始处 */
    q = strchr(p, '"');/* 定位内容结束引号 */
    if (q == NULL)
    {
        return false;
    }

    len = (uint32_t)(q - p);
    if (len >= out_len)
    {
        len = out_len - 1;/* 防止溢出 */
    }
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

/**
 * @brief  解析一条升级通知 payload，提取version和url。
 * @return true=解析出有效的 version 与 url；false=解析失败。
 */
static bool onenet_parse_notify(const char *payload, onenet_ota_notify_t *out)
{
    if (payload == NULL || out == NULL)
    {
        return false;
    }

    if (!json_extract_str(payload, "version", out->version, sizeof(out->version)))
    {
        LOG_W("notify no version field\r\n");
        return false;
    }

    if (!json_extract_str(payload, "url", out->url, sizeof(out->url)))
    {
        LOG_W("notify no url field\r\n");
        return false;
    }

    LOG_I("notify: version=%s url=%s\r\n", out->version, out->url);
    return true;
}

/* ============================ 对外接口 ============================*/
/**
 * @brief  初始化并接入 OneNET：
 * 配置 MQTT 用户信息：client_id（设备 ID）+ username（产品 ID）+ password（token） 
 * 连接OneNET broker
 * 订阅升级主题: 物模型服务调用主题 
 */
bool onenet_mqtt_init(void)
{
    esp_at_mqtt_init();   /* 创建事件信号量 */

    /* MQTT 配置用户信息 */
    if (!esp_at_mqtt_user_cfg(ONENET_DEVICE_NAME, ONENET_PRODUCT_ID, ONENET_TOKEN))
    {
        LOG_E("mqtt user cfg fail\r\n");
        return false;
    }
    LOG_I("mqtt user cfg ok\r\n");

    /* MQTT 连接 OneNET broker */
    if (!esp_at_mqtt_connect(ONENET_MQTT_HOST, ONENET_MQTT_PORT))
    {
        LOG_E("mqtt connect fail\r\n");
        return false;
    }
    LOG_I("mqtt connect ok\r\n");

    /* MQTT 订阅升级主题 */
    if (!esp_at_mqtt_subscribe(ONENET_SUB_TOPIC, 0))
    {
        LOG_E("mqtt subscribe fail\r\n");
        return false;
    }
    LOG_I("mqtt subscribe ok: %s\r\n", ONENET_SUB_TOPIC);

    return true;
}

/**
 * @brief  断开 MQTT 连接（下载固件前调用）后续建立TCP+http GET +进入透传执行下载。
 */
void onenet_mqtt_disconnect(void)
{
    esp_at_mqtt_clean();
}

/**
 * @brief  重连 MQTT 并重新订阅（下载结束后 / 断线恢复时调用）。
 */
bool onenet_mqtt_reconnect(void)
{
    /* 断开后 ESP 可能丢失 MQTT 用户配置，需重新配置鉴权参数 */
    if (!esp_at_mqtt_user_cfg(ONENET_DEVICE_NAME, ONENET_PRODUCT_ID, ONENET_TOKEN))
    {
        return false;
    }
    if (!esp_at_mqtt_connect(ONENET_MQTT_HOST, ONENET_MQTT_PORT))
    {
        return false;
    }
    if (!esp_at_mqtt_subscribe(ONENET_SUB_TOPIC, 0))
    {
        return false;
    }
    return true;
}

/**
 * @brief  查询 MQTT 连接状态。
 */
bool onenet_mqtt_is_connected(void)
{
    return esp_at_mqtt_is_connected();
}

/**
 * @brief  阻塞等待升级通知并缓存进消息结构体中提取内容（带超时，事件驱动）。
 */
bool onenet_mqtt_wait_notify(onenet_ota_notify_t *out, uint32_t timeout_ms)
{
    uint32_t start  = HAL_GetTick();
    uint32_t remain = timeout_ms;

    while (remain > 0)
    {
        char payload[512];

        /* 阻塞等待订阅消息事件（事件驱动） */
        if (!esp_at_mqtt_wait_event(remain))
        {
            return false;   /* 超时无事件 */
        }

        /* 有事件：取走 payload 并解析，有效则返回 */
        if (esp_at_mqtt_poll_payload(payload, sizeof(payload)) &&
            onenet_parse_notify(payload, out))
        {
            return true;
        }
        /* 无效通知，继续等剩余时间 */
        remain = timeout_ms - (HAL_GetTick() - start);
    }

    return false;
}
