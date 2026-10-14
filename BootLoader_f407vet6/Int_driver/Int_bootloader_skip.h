/**
 * @file    Int_bootloader_skip.h
 * @brief   BootLoader 跳转到 App 的接口封装
 * @note    跳转的具体步骤见 Int_bootloader_skip.c。
 */
#ifndef __INT_BOOTLOADER_SKIP_H_
#define __INT_BOOTLOADER_SKIP_H_

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "usart.h"

/* 固件程序数据放在 0x08009000：0x08008000 是 App 区起始，前 4KB 为 XBIN 头信息，
   不能直接跳转，故实际跳转入口为 0x08009000 */
#define APP_START_ADDR        0x08009000
#define APP_END_ADDR          0x08080000
/* SRAM 起止地址：跳转前校验栈顶地址必须落在该区间内 */
#define SRAM_START_ADDR       0x20000000
#define SRAM_END_ADDR         0x20020000

/**
 * @brief  跳转到 App 应用程序
 * @param  app_start_addr App 起始地址
 * @return true 表示跳转成功；false 表示栈顶地址或复位向量校验失败
 */
bool Int_bootloader_jump_to_app(uint32_t app_start_addr);

#endif // __INT_BOOTLOADER_SKIP_H_
