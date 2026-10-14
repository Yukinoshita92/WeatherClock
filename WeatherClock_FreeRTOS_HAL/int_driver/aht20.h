/**
 * @file    aht20.h
 * @brief   AHT20 温湿度传感器驱动接口
 */
#ifndef __AHT20_H
#define __AHT20_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include "i2c.h"

/* AHT20 底层 I2C 驱动（使用 I2C1，引脚定义见下） */
#define I2C_Port         GPIOB
#define SCL_Pin          GPIO_Pin_8
#define SDA_Pin          GPIO_Pin_9
#define SCL_PinSource    GPIO_PinSource8
#define SDA_PinSource    GPIO_PinSource9
/* AHT20 应用层：器件 7 位地址 0x38 左移 1 位后的写/读地址 */
#define ADDRESS_WRITE 0x70
#define ADDRESS_READ 0x71

bool aht20_init(void);                                   /* 初始化并完成上电校准 */
bool aht20_measure(float* temperature,float* humidity);  /* 触发一次测量，输出温度(℃)与湿度(%) */

#endif
