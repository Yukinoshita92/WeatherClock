/**
 * @file    aht20.c
 * @brief   AHT20 温湿度传感器驱动（I2C1）
 * @details 提供初始化校准与单次测量两个接口：
 *            - aht20_init()    等待上电稳定后执行校准，最长等待 100ms；
 *            - aht20_measure() 触发一次测量并输出温度（℃）与湿度（%）。
 * @note    注意事项：
 *            - AHT20 为纯数据流器件，首字节即命令，必须使用 Master_Transmit/Receive；
 *              误用 Mem_Write/Mem_Read 会因寄存器地址被当作命令而时序错乱；
 *            - 内部使用 vTaskDelay，必须在 FreeRTOS 调度器启动后调用。
 */
#include "aht20.h"
#include "FreeRTOS.h"
#include "task.h"

/* 分级日志框架：本模块 TAG=AHT20 */
#define LOG_TAG "AHT20"
#include "log.h"

/* 向 AHT20 发送 len 字节（设备地址已指定，无需寄存器地址） */
static bool aht20_write(uint8_t* send_data,uint16_t len)
{
	HAL_StatusTypeDef State=HAL_I2C_Master_Transmit(&hi2c1,ADDRESS_WRITE,send_data,len,1000);

	return (State == HAL_OK);
}

/* 从 AHT20 接收 len 字节 */
static bool aht20_read(uint8_t* receive_data,uint16_t len)
{
	HAL_StatusTypeDef State=HAL_I2C_Master_Receive(&hi2c1,ADDRESS_READ,receive_data,len,1000);
	return (State == HAL_OK);
}
/* 读取状态字节：先发送 0x71 命令，再读回 1 字节状态 */
static bool aht20_get_state(uint8_t* state)
{
	uint8_t cmd=0x71;   /* 读取状态的固定命令 */
	
	if(!aht20_write(&cmd,1))
	{
		return false;
	}
		
	if(!aht20_read(state,1))
	{
		return false;
	}
	return true;
}

/* 查询上电校准标志（状态位 bit3） */
static bool aht20_is_calibrated(void)
{
	uint8_t state;
	if(!aht20_get_state(&state))
	{
		return false;
	}
	return (state & 0x08)!=0;   /* bit3 为 1 表示已校准 */
}
/* 查询设备忙标志（状态位 bit7） */
static bool aht20_is_busy(void)
{
	uint8_t state;
	if(!aht20_get_state(&state))
	{
		return true;   /* 状态读取失败时按“忙”处理 */
	}
	return (state & 0x80)!=0;   /* bit7 为 1 表示正忙 */
}

/* 等待设备退出忙状态，最长 100ms */
static bool aht20_wait_nbusy(void)
{
	uint32_t timeout=100;
	while(timeout>0)
	{
		if(!aht20_is_busy())   /* 只有明确“不忙”才返回 true */
		{
			return true;
		}
		vTaskDelay(pdMS_TO_TICKS(1));
		timeout--;
	}
	 LOG_W("wait nbusy timeout\r\n");
    return false;   /* 超时即失败 */
}

/* 初始化：等待上电稳定后执行校准，最长等待 100ms */
bool aht20_init(void)
{
	
	vTaskDelay(pdMS_TO_TICKS(40));
	
	if(aht20_is_calibrated())
	{
		return true;
	}
	aht20_write((uint8_t[]){0xBE,0x08,0x00},3);   /* 校准命令 */

	for(int i=0;i<100;i++)
	{
		vTaskDelay(pdMS_TO_TICKS(1));
		if(aht20_is_calibrated())
			return true;
	}
	
	LOG_E("init fail\r\n");
	return false;
}

/* 触发一次测量，输出温度（℃）与湿度（%） */
bool aht20_measure(float* temperature,float* humidity)
{
	uint8_t data[6];
	uint8_t cmd=0x71;
	if(!aht20_write((uint8_t[]){0xAC,0x33,0x00},3))   /* 触发测量命令 */
	{
		LOG_E("measure send cmd fail\r\n");
		return false;
	}
	if(!aht20_wait_nbusy()){LOG_E("measure wait nbusy fail\r\n");return false;}
	if(!aht20_write(&cmd,1)){LOG_E("measure send cmd fail2\r\n");return false;}
	if(!aht20_read(data,6)){LOG_E("measure read fail\r\n");return false;}
	
	uint32_t temperature_raw,humidity_raw;
	humidity_raw=((uint32_t)data[1]<<12)|((uint32_t)data[2]<<4)|(((uint32_t)data[3]&0xF0)>>4);
	temperature_raw=(((uint32_t)data[3]&0x0F)<<16)|((uint32_t)data[4]<<8)|((uint32_t)data[5]);
	/* 原始值满量程为 2^20：湿度换算为 0~100%，温度按 200℃ 满量程并偏移 -50℃ */
	*humidity=((float)humidity_raw*100.0f)/(float)0x100000;
	*temperature=((float)temperature_raw*200.0f)/(float)0x100000-50.0f;
	return true;
}
