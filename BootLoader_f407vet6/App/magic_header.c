/**
 * @file    magic_header.c
 * @brief   XBIN 头部校验与字段访问
 * @details 头部可能来自两处：
 *            1) 内部 Flash 的 App 区起始（已烧录 App 的前 4096 字节）；
 *            2) W25Q64 暂存区（OTA 下载的包，先读 256 字节到 RAM）。
 *          因此本文件将头部指针作为参数传入，做到地址无关，两种情况均可复用。
 * @note    不采用“把头部固化在固定地址并直接解引用”的做法；
 *          保留字段后续可用于扩展升级信息（如签名等）。
 */
#include "magic_header.h"
#include "crc32.h"
#include <stddef.h>

#define MAGIC_HEADER_MAGIC  0x4D414749u   /* "MAGI" */

/**
 * @brief  校验给定指针处的头部（魔数 + 头部自身 CRC）
 * @param  header 头部指针
 * @return true 表示头部有效；false 表示无效
 */
bool magic_header_validate(const magic_header_t *header)
{
    uint32_t ccrc;

    if (header == NULL)
    {
        return false;
    }

    /* 1. 魔数错误 => 根本不是 XBIN 文件 */
    if (header->magic != MAGIC_HEADER_MAGIC)
    {
        return false;
    }

    /* 2. 头部自身 CRC：覆盖 0x00 到 this_crc32 之前（不含） */
    ccrc = crc32((const uint8_t *)header, offsetof(magic_header_t, this_crc32));   /* 本地重算头部 CRC */
    if (ccrc != header->this_crc32)
    {
        return false;
    }

    return true;
}

/** @brief 获取头部中的打包类型 */
magic_header_type_t magic_header_get_type(const magic_header_t *header)
{
    return (magic_header_type_t)header->data_type;
}

/** @brief 获取头部中标注的负载在整包固件中的偏移（即负载在文件中的起始位置） */
uint32_t magic_header_get_offset(const magic_header_t *header)
{
    return header->data_offset;
}

/** @brief 获取头部中标注的负载要烧录到的目标硬件起始地址 */
uint32_t magic_header_get_address(const magic_header_t *header)
{
    return header->data_address;
}

/** @brief 获取头部中标注的负载长度 */
uint32_t magic_header_get_length(const magic_header_t *header)
{
    return header->data_length;
}

/** @brief 获取头部中标注的负载 CRC32 */
uint32_t magic_header_get_crc32(const magic_header_t *header)
{
    return header->data_crc32;
}

/** @brief 获取头部中标注的目标硬件机型 */
uint32_t magic_header_get_hw_model(const magic_header_t *header)
{
    return header->hw_model;
}

/** @brief 获取 BL 最低要求版本 */
uint32_t magic_header_get_bl_version_min(const magic_header_t *header)
{
    return header->bl_version_min;
}
