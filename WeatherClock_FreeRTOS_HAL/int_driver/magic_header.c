/**
 * @file    magic_header.c
 * @brief   XBIN 头部校验与字段读取
 * @details 头部可能来自内部 Flash 的 App 区起始，也可能来自 W25Q64 暂存区，
 *          因此本文件把头部指针作为参数传入，做到地址无关、两处均可复用。
 *          校验内容为魔数（0x4D414749 = "MAGI"）与头部自身 CRC32。
 * @note    注意事项：
 *            - 校验不通过时应立即返回，不得使用头部中的任何字段；
 *            - 所有 get 接口都应在校验通过之后调用。
 */
#include "magic_header.h"
#include "crc32.h"
#include <stddef.h>

#define MAGIC_HEADER_MAGIC  0x4D414749u   /* "MAGI" */

/* 校验给定指针处的头部：魔数 + 头部自身 CRC */
bool magic_header_validate(const magic_header_t *header)
{
    uint32_t ccrc;

    if (header == NULL)
    {
        return false;
    }

    /* 1. 魔数不匹配，说明不是 XBIN 文件 */
    if (header->magic != MAGIC_HEADER_MAGIC)
    {
        return false;
    }

    /* 2. 头部自身 CRC：覆盖 0x00 到 this_crc32 之前（不含） */
    ccrc = crc32((const uint8_t *)header, offsetof(magic_header_t, this_crc32));
    if (ccrc != header->this_crc32)
    {
        return false;
    }

    return true;
}
/* 读取头部中的打包类型 */
magic_header_type_t magic_header_get_type(const magic_header_t *header)
{
    return (magic_header_type_t)header->data_type;
}
/* 读取负载在整包固件中的偏移 */
uint32_t magic_header_get_offset(const magic_header_t *header)
{
    return header->data_offset;
}
/* 读取负载要烧录到的目标起始地址 */
uint32_t magic_header_get_address(const magic_header_t *header)
{
    return header->data_address;
}
/* 读取负载长度 */
uint32_t magic_header_get_length(const magic_header_t *header)
{
    return header->data_length;
}
/* 读取负载 CRC32 */
uint32_t magic_header_get_crc32(const magic_header_t *header)
{
    return header->data_crc32;
}
/* 读取目标硬件机型 */
uint32_t magic_header_get_hw_model(const magic_header_t *header)
{
    return header->hw_model;
}
/* 读取 BL 最低要求版本 */
uint32_t magic_header_get_bl_version_min(const magic_header_t *header)
{
    return header->bl_version_min;
}
