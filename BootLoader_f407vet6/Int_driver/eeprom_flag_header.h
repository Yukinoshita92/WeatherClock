/**
 * @file    eeprom_flag_header.h
 * @brief   OTA 标志位在 AT24C02 中的存放约定（供 BL 与 App 两个工程共用）
 * @details OTA 配置区位于 AT24C02（256B，8B/页），固定占用 0x00~0x0F 共 16 字节。
 *          各宏均为 EEPROM 地址（充当 BKUP 寄存器的 DRx，用于存放掉电不丢失的信息），
 *          其中 0x0E~0x0F 存放覆盖 0x00~0x0D 的 CRC16(XMODEM) 校验值。
 * @note    EEP_OTA_MAGIC 的作用：标识当前存储器是否已初始化（出厂默认 0xFF）、是否被损坏。
 *          正常写入时该位置被写入约定值 0xA5；若掉电导致该值被改写，说明当前 16 字节标志
 *          不可靠、不再使用。它与 0x0E/0x0F 的 CRC 一起校验标志是否可靠：即使掉电损坏后
 *          magic 恰好仍是 0xA5，其余字段被破坏也能由 CRC 把关。若标志不可靠，则全部擦除
 *          并重新初始化（置 magic = 0xA5）。
 */
#ifndef __EEPROM_FLAG_HEADER_H
#define __EEPROM_FLAG_HEADER_H

#include <stdint.h>

/* OTA 配置区内的各标志地址（均为 EEPROM 地址） */
#define EEP_OTA_MAGIC           0x00   /* 1B  初始化标记 = 0xA5 */
#define EEP_OTA_UPDATE_FLAG     0x01   /* 1B  0x5A = 需升级，0x00 = 无 */
#define EEP_OTA_BOOT_COUNT      0x02   /* 1B  启动计数，>= 3 触发回滚 */
#define EEP_OTA_FAIL_COUNT      0x03   /* 1B  升级失败计数，>= 3 停止自动升级 */
#define EEP_OTA_ATTEMPT_VER     0x04   /* 4B  上次尝试的目标版本号（u32，大端） */
#define EEP_OTA_OLD_LEN         0x08   /* 4B  旧固件长度（u32，大端） */
#define EEP_OTA_BACKUP_VALID    0x0C   /* 1B  0x5A = 备份有效 */
#define EEP_OTA_RESERVED        0x0D   /* 1B  保留字段 后续可以拓展 */
#define EEP_OTA_CRC_H           0x0E   /* 1B  CRC16 高字节 */
#define EEP_OTA_CRC_L           0x0F   /* 1B  CRC16 低字节 */

/* 各标志的约定取值与上限 */
#define EEP_OTA_MAGIC_VAL         0xA5u 
#define EEP_OTA_UPDATE_NEED       0x5Au
#define EEP_OTA_UPDATE_NONE       0x00u
#define EEP_OTA_BOOT_COUNT_MAX    3u
#define EEP_OTA_FAIL_COUNT_MAX    3u
#define EEP_OTA_BACKUP_VALID_VAL  0x5Au

/***** 版本号格式统一 *****/
/* 将版本号 主版本.次版本.修订号（各 0~255）打包进一个 u32。 */
#define OTA_VER(maj,min,pat)  (((uint32_t)(maj)<<16) | ((uint32_t)(min)<<8) | (uint32_t)(pat))

#endif /* __EEPROM_FLAG_HEADER_H */
