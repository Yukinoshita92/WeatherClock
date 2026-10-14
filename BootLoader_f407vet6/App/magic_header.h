/**
 * @file    magic_header.h
 * @brief   XBIN 自描述固件头部 - 升级包的“身份证”
 * @details 一个升级文件（.xbin）= 4096 字节头部 + 固件负载（payload）。
 *          头部内嵌“什么固件 / 目标地址 / 长度 / CRC / 版本 / 硬件机型”，
 *          因此 BootLoader 在决定是否拷贝进内部 Flash 前，先校验头部。
 *
 *          与旧 CAN IAP 方案的区别：旧方案将“长度 + CRC”存放在 W25Q64 单独的
 *          元数据区，分散存放、容易错位；XBIN 把这些信息直接做进固件文件本身，
 *          随固件一起传输，原生支持版本管理，并可在机型不符时拒绝烧录。
 * @note    本结构体为 256 字节、自然对齐（无填充），且必须与
 *          scripts/gen_magic_header.py 逐字段一一对应。
 */
#ifndef __MAGIC_HEADER_H__
#define __MAGIC_HEADER_H__

#include <stdbool.h>
#include <stdint.h>

/** @brief 打包类型：固件代码 / 资源包 */
typedef enum
{
    MAGIC_HEADER_TYPE_APP      = 0, /* 固件代码，烧录到内部 Flash */
    MAGIC_HEADER_TYPE_RESOURCE = 1, /* 资源包（字库/图片/配置），存到 W25Q64 */
} magic_header_type_t;

/** @brief 本产品的硬件机型 ID（机型不符的包将被拒绝，防止硬件误刷） */
#define MAGIC_HEADER_HW_MODEL_WEATHERCLOCK_F407  0x0001u

/* 结构体共 256 字节，各成员均为 4 字节对齐，自然对齐、无填充 */
typedef struct
{                              /* 偏移 解释 */
    uint32_t magic;            /* 0x00 魔数 固定值 0x4D414749 = "MAGI" 用于识别XBIN文件 */
    uint32_t bitmask;          /* 0x04 有效字段位掩码，0 表示所有字段全部有效 */
    uint32_t reserved1[6];     /* 0x08 保留字段1 */

    uint32_t data_type;        /* 0x20 打包类型，见 magic_header_type_t */
    uint32_t data_offset;      /* 0x24 负载在文件中的偏移，恒为 4096 */
    uint32_t data_address;     /* 0x28 负载烧录到目标硬件的起始地址 */
    uint32_t data_length;      /* 0x2C 负载长度（字节） */
    uint32_t data_crc32;       /* 0x30 负载 CRC32 */

    uint32_t hw_model;         /* 0x34 硬件机型（新增：防错刷） */
    uint32_t bl_version_min;   /* 0x38 BL 最低要求版本（新增） */
    uint32_t reserved2[9];     /* 0x3C 保留字段2 */

    char     version[128];     /* 0x60 版本字符串，如 "v1.2.0-260816-1430" */

    uint32_t reserved3[6];     /* 0xE0 保留字段3 */
    uint32_t this_address;     /* 0xF8 本头部实际所在地址 一般用来调试 */
    uint32_t this_crc32;       /* 0xFC 头部自身 CRC（覆盖 0x00..0xFB） */
} magic_header_t;

/**
 * @brief  校验给定指针处的头部（魔数 + 头部自身 CRC）
 * @param  header 头部指针
 * @return true 表示头部有效；false 表示无效
 */
bool magic_header_validate(const magic_header_t *header);

/* 字段读取接口（在校验通过之后调用） */
magic_header_type_t magic_header_get_type(const magic_header_t *header);
uint32_t magic_header_get_offset(const magic_header_t *header);
uint32_t magic_header_get_address(const magic_header_t *header);
uint32_t magic_header_get_length(const magic_header_t *header);
uint32_t magic_header_get_crc32(const magic_header_t *header);
uint32_t magic_header_get_hw_model(const magic_header_t *header);
uint32_t magic_header_get_bl_version_min(const magic_header_t *header);

#endif /* __MAGIC_HEADER_H__ */
