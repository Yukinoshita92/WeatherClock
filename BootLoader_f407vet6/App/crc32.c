/**
 * @file    crc32.c
 * @brief   软件 CRC32（查表法，兼容 zlib）
 * @details 作用范围：整个固件（几十 ~ 几百 KB）的完整性校验，不是单帧校验。
 *          计算结果与 Python 的 zlib.crc32 完全一致，两端可直接比对；
 *          App 端校验下载数据时可直接原样复用本文件。
 */
#include "crc32.h"

/* 运行时构建 256 项查找表，表放 RAM（1024 字节），懒加载 */
static uint32_t crc32_table[256];
static int crc32_table_ready = 0;

/* 使用反转的 IEEE 多项式 0xEDB88320 构建 256 项查找表
 * 说明：标准多项式是 0x04C11DB7；zlib 采用“低位优先（LSB first）”实现，
 *       等价的写法就是把多项式按位反转成 0xEDB88320，运行时向右移位。 */
static void crc32_make_table(void)
{
    uint32_t i;
    int j;
    for (i = 0; i < 256; i++)
    {
        uint32_t c = i;                       /* 第 i 项从字节 i 本身开始算 */
        for (j = 0; j < 8; j++)
        {
            /* 最低位为 1 才异或反转多项式，否则只右移 */
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_ready = 1;
}

uint32_t crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;               /* 初值全 1（zlib 规定） */
    uint32_t i;

    if (!crc32_table_ready)                   /* 首次调用：按需建表 */
    {
        crc32_make_table();
    }

    /* 查表法核心（低位优先，一次 1 字节）：
     *   (crc ^ data[i]) & 0xFF -> 参与本次运算的索引
     *   (crc >> 8) ^ 表值      -> 相当于逐位算法跑 8 轮 */
    for (i = 0; i < len; i++)
    {
        crc = crc32_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFFu;                 /* 结果取反（final xor）后返回 */
}
