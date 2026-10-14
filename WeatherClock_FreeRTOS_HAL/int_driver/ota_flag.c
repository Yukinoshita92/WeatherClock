/**
 * @file    ota_flag.c
 * @brief   OTA 升级标志读写（App 端，存放于 AT24C02 配置区）
 * @details 标志区共 14 字节，另附 2 字节 CRC16(XMODEM) 校验，地址分配见
 *          eeprom_flag_header.h。所有读写都先做 magic + CRC 自检，自检不通过
 *          （未初始化或掉电损坏）则整体重置为默认值。
 * @note    注意事项：
 *            - 标志值必须与 BootLoader 侧定义完全一致，否则 BL 无法识别升级请求；
 *            - App 正常启动后需调用 ota_flag_clear_boot_count()，避免 BL 误判为启动失败；
 *            - 首次使用前请先调用 ota_flag_init()。
 */

#include "ota_flag.h"
/* EEPROM 配置区 + AT24C02 驱动 */
#include "eeprom_flag_header.h"
#include "Int_AT24C02.h"
#include "byteorder.h"
#include <string.h>

/* 单独封装的 CRC16：poly 0x1021、init 0x0000，覆盖配置区 0x00~0x0D 的 14 字节标志 */
static uint16_t ota_flag_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0x0000u;
    uint16_t i; int j;
    for (i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x8000u) crc = (uint16_t)((crc << 1) ^ 0x1021u);
            else               crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}
/* 读配置区前 14 字节标志并校验 magic+CRC；返回 0=有效，-1=脏数据 */
static int ota_flag_read_all(uint8_t buf[14])
{
    uint8_t crc[2];
    /* 读取 14 字节标志与 2 字节 CRC */
    INT_AT24C02_Read_Bytes(EEP_OTA_MAGIC, buf, 14);
    INT_AT24C02_Read_Bytes(EEP_OTA_CRC_H, crc, 2);
     if (buf[0] != EEP_OTA_MAGIC_VAL) return -1;   /* magic 损坏或未初始化，交给调用方重置 */
    /* 用 byteorder.h 读 2 字节大端 */
    uint16_t expect_crc = get_u16_be(crc);
    uint16_t calc   = ota_flag_crc16(buf, 14);   /* 本地重算 CRC 后与存储值比对 */
    return (calc == expect_crc) ? 0 : -1;
}
/* 对调整后的标志数组重算 CRC，并把标志与 CRC 一起写回 EEPROM */
static void ota_flag_revise(uint8_t buf[14])
{
    uint16_t crc = ota_flag_crc16(buf, 14);
    /* 用 byteorder.h 写 2 字节大端 */
    uint8_t c[2];
    put_u16_be(c, crc);
    INT_AT24C02_Write_Bytes(EEP_OTA_MAGIC, buf, 14);
    INT_AT24C02_Write_Bytes(EEP_OTA_CRC_H, c, 2);
}
/* 读取指定单字节位置的标志（脏数据时先重置为默认值） */
static uint8_t ota_flag_read_u8(uint8_t addr)
{
    uint8_t buf[14] = {0};
    if (ota_flag_read_all(buf) != 0) { memset(buf, 0, 14); buf[0] = EEP_OTA_MAGIC_VAL; }
    return buf[addr];
}
/* 写入指定单字节位置的标志 */
static void ota_flag_write_u8(uint8_t addr, uint8_t val)
{
    uint8_t buf[14] = {0};
    if (ota_flag_read_all(buf) != 0) { memset(buf, 0, 14); buf[0] = EEP_OTA_MAGIC_VAL; }
    buf[addr] = val;
    ota_flag_revise(buf);
}
/* 读取指定 4 字节大端位置的标志（旧目标版本 / 旧固件长度） */
static uint32_t ota_flag_read_u32_be(uint8_t addr)
{
    uint8_t buf[14] = {0};
    if (ota_flag_read_all(buf) != 0) { memset(buf, 0, 14); buf[0] = EEP_OTA_MAGIC_VAL; }
    /* 用 byteorder.h 读 4 字节大端 */
    return get_u32_be(&buf[addr]);
}
/* 写入指定 4 字节大端位置的标志（旧目标版本 / 旧固件长度） */
static void ota_flag_write_u32_be(uint8_t addr, uint32_t value)
{
    uint8_t buf[14] = {0};
    if (ota_flag_read_all(buf) != 0) { memset(buf, 0, 14); buf[0] = EEP_OTA_MAGIC_VAL; }
    /* 用 byteorder.h 写 4 字节大端 */
    put_u32_be(&buf[addr], value);
    ota_flag_revise(buf);
}

/**
 * @brief 初始化 EEPROM 标志存储：写入 magic 并初始化其余标志
 *        （若 magic+CRC 自检不通过，说明未初始化或已损坏，则整体写默认值）
 */
void ota_flag_init(void)
{
    /* 读配置区做 magic+CRC 自检，脏数据则写默认值 */
    uint8_t buf[14] = {0};
    if (ota_flag_read_all(buf) != 0)
    {
        memset(buf, 0, 14);
        buf[EEP_OTA_MAGIC] = EEP_OTA_MAGIC_VAL;
        ota_flag_revise(buf);
    }
}

/**
 * @brief  置"需要升级"标志（配置区 EEP_OTA_UPDATE_FLAG = EEP_OTA_UPDATE_NEED）。
 */
void ota_flag_set_need_update(void)
{
    ota_flag_write_u8(EEP_OTA_UPDATE_FLAG, EEP_OTA_UPDATE_NEED);
}

/**
 * @brief  清除"需要升级"标志（配置区 EEP_OTA_UPDATE_FLAG = EEP_OTA_UPDATE_NONE）。
 */
void ota_flag_clear_update(void)
{
    ota_flag_write_u8(EEP_OTA_UPDATE_FLAG, EEP_OTA_UPDATE_NONE);
}

/**
 * @brief  清零启动计数（配置区 EEP_OTA_BOOT_COUNT = 0）。
 */
void ota_flag_clear_boot_count(void)
{
    ota_flag_write_u8(EEP_OTA_BOOT_COUNT, 0u);
}

/**
 * @brief  读取当前启动计数（配置区 EEP_OTA_BOOT_COUNT）。
 */
uint32_t ota_flag_get_boot_count(void)
{
    return ota_flag_read_u8(EEP_OTA_BOOT_COUNT);
}

/* 读取升级失败计数 */
uint8_t ota_flag_get_fail_count(void)
{
    return ota_flag_read_u8(EEP_OTA_FAIL_COUNT);
}
/* 清零升级失败计数 */
void ota_flag_reset_fail_count(void)
{
    ota_flag_write_u8(EEP_OTA_FAIL_COUNT, 0u);
}
/* 升级失败计数 +1，达到上限后不再累加 */
void ota_flag_inc_fail_count(void)
{
    uint8_t cnt = ota_flag_read_u8(EEP_OTA_FAIL_COUNT);
    if (cnt < EEP_OTA_FAIL_COUNT_MAX)
    {
        ota_flag_write_u8(EEP_OTA_FAIL_COUNT, (uint8_t)(cnt + 1u));
    }
}
/* 设置本次尝试升级的目标版本（u32 打包，大端存 4 字节） */
void ota_flag_set_attempt_ver(uint32_t ver)
{
    ota_flag_write_u32_be(EEP_OTA_ATTEMPT_VER, ver);
}
/* 获取本次尝试升级的目标版本 */
uint32_t ota_flag_get_attempt_ver(void)
{
    return ota_flag_read_u32_be(EEP_OTA_ATTEMPT_VER);
}
