/**
 * @file    crc16.c
 * @brief   软件 CRC16（查表法，XMODEM 标准）
 * @details 作用范围：串口升级协议的“单帧”校验，每帧最大 4KB（见 bl_packet.c）。
 *          参数与 PC 端工具 SerialUpgrader 的 CRC.cs 严格一致，
 *          否则每一帧的 CRC 校验都会失败，导致升级无法进行。
 */
#include "crc16.h"

/* 运行时构建 XMODEM CRC16 表（多项式 0x1021）
 * 采用“懒加载”：第一次调用 crc16() 时才建表，避免开机阶段的无效开销。
 * 代价是表放在 RAM（512 字节），换来的是 Flash 里少一份常量表。 */
static uint16_t crc16_table[256];
static int crc16_table_ready = 0;

/* 生成 256 项查找表：
 * 第 i 项 = “把 i 当作高 8 位、后面补 8 个 0”这一段数据的 CRC 结果。
 * 这里用最直观的逐位除法来算，代价只在上电后首帧支付一次。 */
static void crc16_make_table(void)
{
    uint32_t i;
    int j;
    for (i = 0; i < 256; i++)                 /* i 即待处理的那 1 字节 */
    {
        uint16_t crc = (uint16_t)(i << 8);    /* 先放到高位，模拟串行输入 */
        for (j = 0; j < 8; j++)               /* 逐位做“判断最高位 -> 左移/异或” */
        {
            /* 最高位为 1 才异或多项式 0x1021，否则只左移 */
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
        crc16_table[i] = crc;
    }
    crc16_table_ready = 1;
}

uint16_t crc16(const uint8_t *buf, uint32_t len)
{
    uint16_t crc = 0x0000u;                   /* XMODEM 规定初值 = 0x0000 */
    uint32_t i;

    if (!crc16_table_ready)                   /* 首次调用：按需建表 */
    {
        crc16_make_table();
    }

    /* 查表法核心（一次吃掉 1 字节）：
     *   (crc >> 8) ^ buf[i]  -> 取出参与本次运算的索引
     *   (crc << 8) ^ 表值     -> 等于逐位算法跑 8 轮的结果
     * 也就是说：8 次“移位+判断+异或”被压缩成 1 次查表 + 1 次异或。 */
    for (i = 0; i < len; i++)
    {
        crc = (uint16_t)((crc << 8) ^ crc16_table[((crc >> 8) ^ buf[i]) & 0x00FFu]);
    }

    return crc;                               /* final xor = 0x0000，直接返回 */
}
