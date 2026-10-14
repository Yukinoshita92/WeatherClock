/**
 * @file    ota_version.h
 * @brief   XBIN 固件版本号解析与比较接口
 */
#ifndef __OTA_VERSION_H
#define __OTA_VERSION_H
#include <stdint.h>
#include <stddef.h>

/* 将 "v1.2.3"（可选的前缀 v/V，可选的后缀 "-xxx"）解析为打包的 u32：
 * (major<<16)|(minor<<8)|patch。固定 3 段，缺失段补 0，每段钳位到 0~255，
 * 多余的段忽略。解析失败返回 0。 */
uint32_t ota_version_to_u32(const char *s);

/* 比较 a 与 b，返回 -1/0/1 分别表示 a<b / a==b / a>b（按 major>minor>patch 顺序）。 */
int ota_version_compare(const char *a, const char *b);

#endif
