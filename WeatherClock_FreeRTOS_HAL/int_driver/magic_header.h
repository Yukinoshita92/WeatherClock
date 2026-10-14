/**
 * @file    magic_header.h
 * @brief   XBIN 固件头部结构与字段访问接口
 * @details 一个升级包 = 4096 字节头部 + 固件负载。头部内嵌固件类型、负载偏移、
 *          烧录地址、长度、CRC32、版本号与硬件机型，BootLoader 在拷贝前先校验头部。
 *          结构体共 256 字节、自然对齐，各字段按小端存放。
 * @note    注意事项：
 *            - 结构体布局必须与打包脚本逐字段一一对应，修改需双方同步；
 *            - 头部自身 CRC 覆盖 0x00~0xFB 共 252 字节；
 *            - 机型字段用于防错刷，机型不符的包必须拒绝。
 */
#ifndef __MAGIC_HEADER_H__
#define __MAGIC_HEADER_H__

#include <stdbool.h>
#include <stdint.h>

/* 打包类型：固件代码 / 资源包 */
typedef enum
{
    MAGIC_HEADER_TYPE_APP      = 0, /* 固件代码，烧录到内部 Flash */
    MAGIC_HEADER_TYPE_RESOURCE = 1, /* 资源包（字库/图片/配置），存到 W25Q64 */
} magic_header_type_t;

/* 本产品的硬件机型 ID（机型不符的包将被拒绝） 防止硬件误刷 */
#define MAGIC_HEADER_HW_MODEL_WEATHERCLOCK_F407  0x0001u

/* 头部结构体共 256 字节，各字段均 4 字节对齐，无填充 */
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

/* 校验给定指针处的头部：魔数 + 头部自身 CRC */
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
