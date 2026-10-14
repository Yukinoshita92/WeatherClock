/**
 * @file    ota.h
 * @brief   WiFi OTA 升级任务接口（App 端）
 * @details App 周期性（默认 30 秒）通过 WiFi 访问固定固件 URL：若有新固件，则经 ESP
 *          透传下载到 W25Q64 暂存区，校验通过后置"需要升级"标志并复位，由 BootLoader
 *          接管完成真正的 Flash 拷贝。
 * @note    注意事项：
 *            - OTA 任务必须在 WiFi 连接成功之后创建（ota_init 应在 wifi_wait_connect
 *              之后调用），否则访问网络会失败；
 *            - 本任务为低优先级后台任务，不阻塞 UI 与主循环。
 */
#ifndef __OTA_H__
#define __OTA_H__

#include <stdint.h>

/* ================= 下载结果枚举 ================= */
typedef enum
{
    OTA_RESULT_OK          = 0,  /* 下载并校验成功，已置位待升级 */
    OTA_RESULT_NO_UPDATE   = 1,  /* 无新固件或版本一致，无需升级 */
    OTA_RESULT_CONNECT_FAIL= 2,  /* 网络连接失败 */
    OTA_RESULT_DOWNLOAD_FAIL=3,  /* 下载过程失败 */
    OTA_RESULT_CRC_FAIL    = 4,  /* CRC 校验失败 */
    OTA_RESULT_HEADER_FAIL = 5,  /* 头部校验失败 */
    OTA_RESULT_FLASH_FAIL  = 6,  /* W25Q64 写入失败 */
} ota_result_t;

/**
 * @brief  初始化并创建 OTA 后台任务
 * @note   在 WiFi 连接成功后调用一次；内部创建低优先级任务并周期性轮询。
 */
void ota_init(void);

#endif /* __OTA_H__ */
