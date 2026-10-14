/**
 * @file    board.c
 * @brief   板级初始化与 FreeRTOS 错误钩子实现
 * @note    本模块日志 TAG = "BOARD"
 */
#include "FreeRTOS.h"
#include "task.h"
#include "app.h"
#include "aht20.h"
#include "usart.h"

/* 分级日志框架：本模块 TAG=BOARD */
#define LOG_TAG "BOARD"
#include "log.h"

/* 板级初始化：打印编译时间戳并初始化板载传感器 */
void board_init(void)
{
	LOG_I("Build Date: %s %s\n", __DATE__, __TIME__);   /* 编译时的日期与时间 */

	aht20_init();
}

/* FreeRTOS 断言钩子：打印断言所在的文件与行号 */
void vAssertCalled(const char *file, int line)
{
    LOG_E("Assert Called: %s(%d)\n", file, line);
}

/* 任务栈溢出钩子：打印溢出任务名后断言 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    LOG_E("Stack Overflowed: %s\n", pcTaskName);
    configASSERT(0);
}

/* 动态内存分配失败钩子 */
void vApplicationMallocFailedHook(void)
{
    LOG_E("Malloc Failed\n");
    configASSERT(0);
}
