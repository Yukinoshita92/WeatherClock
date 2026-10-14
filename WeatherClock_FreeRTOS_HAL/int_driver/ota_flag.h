/**
 * @file    ota_flag.h
 * @brief   OTA 升级标志读写接口（App 端）
 * @details 标志掉电保存在 AT24C02 配置区（地址分配见 eeprom_flag_header.h），是 App
 *          与 BootLoader 之间传递状态的桥梁：
 *            - 升级标志：OTA 校验成功后置位，BL 上电据此决定是否升级；
 *            - 启动计数：App 启动成功后清零，避免 BL 误判为启动失败而回滚；
 *            - 失败计数与目标版本：用于限制坏固件反复重下。
 * @note    注意事项：
 *            - 首次使用前调用 ota_flag_init()，内部会做 magic + CRC 自检；
 *            - 标志值必须与 BootLoader 侧定义完全一致；
 *            - 各接口均可安全重复调用。
 */
#ifndef __OTA_FLAG_H__
#define __OTA_FLAG_H__

#include <stdint.h>

/* ================= 与 BootLoader 保持一致的定义 ================= */

/* ================= 接口函数 ================= */

/**
 * @brief  初始化 EEPROM 标志区（magic+CRC 自检，脏数据则重置为默认值）。
 * @note   应在 App 首次访问标志区前调用一次；可安全重复调用。
 */
void ota_flag_init(void);

/**
 * @brief  置"需要升级"标志。
 * @note   OTA 下载并校验成功后调用；随后应软件复位（NVIC_SystemReset），
 *         复位后 BootLoader 读取该标志执行升级。
 */
void ota_flag_set_need_update(void);

/**
 * @brief  清除"需要升级"标志。
 * @note   一般由 BootLoader 升级成功后清除；App 端正常无需调用，保留接口。
 */
void ota_flag_clear_update(void);

/**
 * @brief  清零启动计数。
 * @note   App 启动成功、业务初始化（时钟/UI/WiFi）完成后调用，防止 BL 误回滚。
 */
void ota_flag_clear_boot_count(void);

/**
 * @brief  读取当前启动计数。
 * @return 启动计数值（调试/日志用）。
 */
uint32_t ota_flag_get_boot_count(void);

/* 升级失败计数：读 / 清零（由 App 判定是否停止自动升级；+1 在 BL 回滚侧完成） */
uint8_t  ota_flag_get_fail_count(void);
void     ota_flag_reset_fail_count(void);
void     ota_flag_inc_fail_count(void);   /* 失败计数 +1 */
/* 本次尝试升级的目标版本（u32 打包，见 OTA_VER）；未设置时为 0 */
void     ota_flag_set_attempt_ver(uint32_t ver);
uint32_t ota_flag_get_attempt_ver(void);

#endif /* __OTA_FLAG_H__ */
