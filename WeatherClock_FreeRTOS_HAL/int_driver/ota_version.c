/**
 * @file    ota_version.c
 * @brief   固件版本号解析与比较（v/V x.x.x）
 * @details 将版本字符串解析为 32 位打包值 (major<<16)|(minor<<8)|patch：
 *          最多 3 段，缺失段补 0，每段钳位到 0~255，解析失败返回 0；
 *          ota_version_compare() 基于该打包值给出 -1 / 0 / 1 的比较结果。
 * @note    注意事项：
 *            - 仅支持可选的 v/V 前缀与 "." 分隔的纯数字段，不支持 "-alpha" 之类后缀；
 *            - 段数超过 3 段时，多余部分被忽略。
 */
#include "ota_version.h"

/**
 * @brief  解析版本字符串为 32 位打包值
 * @param  version 版本字符串，形如 "v1.2.0" / "1.2"
 * @return (major<<16)|(minor<<8)|patch；解析失败返回 0
 */
uint32_t ota_version_to_u32(const char *version)
{
    uint32_t seg[3] = {0u, 0u, 0u};/* 三段版本号：major.minor.patch */
    int idx = 0;/* 当前段索引 */
    const char *p = version;
    if (p == NULL) return 0u;/* 入参无效：解析失败并返回 0，上层据此判定不升级 */
    if (*p == 'v' || *p == 'V') p++;/* 跳过可选的 v/V 前缀 */
    while (idx < 3 && *p)/* 最多解析 3 段 */
    {
        uint32_t seg_value = 0u;/* 当前段解析出的数值 */
        int at_least_one = 0;/* 本段是否解析到数字 */
        while (*p >= '0' && *p <= '9')/* 逐位解析数字字符，遇非数字即结束本段 */
        {
            seg_value = seg_value * 10u + (uint32_t)(*p - '0');/* 多位数字按十进制累加 */
            if (seg_value > 255u) seg_value = 255u;/* 每段钳位到 0~255 */
            p++; 
            at_least_one = 1;/* 本段有效 */
        }
        if (!at_least_one)/* 本段没有数字，视为格式错误 */
        {
            if (idx == 0) return 0u;  /* 第一段必须是数字 否则解析失败 */
            break;
        }
        seg[idx++] = seg_value;
        if (*p != '.') break;/* 不是 "." 分隔，结束解析 */
        p++;
    }
    return (seg[0] << 16) | (seg[1] << 8) | seg[2];
}

/**
 * @brief  比较两个版本字符串的新旧
 * @param  a 版本字符串 A
 * @param  b 版本字符串 B
 * @return a 新于 b 返回 1；a 旧于 b 返回 -1；相同返回 0
 * @note   比较基于 ota_version_to_u32() 打包后的 32 位数值。
 */
int ota_version_compare(const char *a, const char *b)
{
    uint32_t ua = ota_version_to_u32(a);
    uint32_t ub = ota_version_to_u32(b);
    if (ua > ub) return 1;
    if (ua < ub) return -1;
    return 0;
}
