/**
 * @file    crc32.c
 * @brief   软件 CRC32 实现（查表法，与 zlib.crc32 一致）
 * @details 首次调用时构建 256 项查找表（驻留 RAM），之后逐字节查表累加；
 *          计算结果与 PC 端 zlib.crc32 / Python 完全一致，便于两端比对。
 * @note    注意事项：
 *            - 查找表为惰性构建且非线程安全，多任务环境建议先预热调用一次；
 *            - 分块数据需自行维护中间值，本文件不提供流式接口。
 */
#include "crc32.h"

static uint32_t crc32_table[256];    /* 256 项 CRC32 查找表 */
static int crc32_table_ready = 0;    /* 查找表是否已构建 */

/* 使用反转的 IEEE 多项式 0xEDB88320 构建 256 项查找表 */
static void crc32_make_table(void)
{
    uint32_t i;
    int j;
    for (i = 0; i < 256; i++)
    {
        uint32_t c = i;
        for (j = 0; j < 8; j++)
        {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_ready = 1;
}

/* 计算 data[0..len-1] 的 CRC32（初值与最终异或均为 0xFFFFFFFF） */
uint32_t crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;

    if (!crc32_table_ready)
    {
        crc32_make_table();
    }

    for (i = 0; i < len; i++)
    {
        crc = crc32_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFFu;
}
