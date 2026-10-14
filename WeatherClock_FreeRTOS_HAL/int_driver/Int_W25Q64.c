/**
 * @file    Int_W25Q64.c
 * @brief   W25Q64 SPI Flash 驱动（8MB，SPI1）
 * @details 封装读、写与擦除，入参支持"块/扇区/页/页内地址"或直接 32 位地址两种形式：
 *            - 写：按 256 字节页边界自动拆分，可一次性传入任意长度；
 *            - 读：地址自动自增，可跨页连续读取；
 *            - 擦除：最小单元为 1 个扇区（4KB）。
 * @note    注意事项：
 *            - 读数据、读 ID、读状态寄存器无需写使能；写与擦除必须先写使能
 *              （驱动内部已处理）；
 *            - 片选拉低/拉高、忙等待与超时判断均由驱动内部完成；
 *            - 擦除耗时较长，驱动内已延时等待，连续擦除大区域请预留足够时间。
 */
#include "Int_W25Q64.h"

/* 分级日志框架：本模块 TAG=W25Q64 */
#define LOG_TAG "W25Q64"
#include "log.h"

/* 片选拉低：起始位，CS 接 PD6 */
static void INT_W25Q64_Start(void)
{
    HAL_GPIO_WritePin(W25Q64_CS_GPIO_Port, W25Q64_CS_Pin, GPIO_PIN_RESET);
}
/* 片选拉高：结束位，CS 接 PD6 */
static void INT_W25Q64_Stop(void)
{
    HAL_GPIO_WritePin(W25Q64_CS_GPIO_Port, W25Q64_CS_Pin, GPIO_PIN_SET);
}
/* 写入一个字节 */
static void INT_W25Q64_Write_Byte(uint8_t data)
{
    HAL_SPI_Transmit(&hspi1,&data,1,100);
}
/* 读取一个字节 */
static uint8_t INT_W25Q64_Read_Byte(void)
{
    uint8_t data;
    HAL_SPI_Receive(&hspi1,&data,1,100);
    return data;
}
/* 等待芯片空闲（读状态寄存器，直到忙位清零或超时） */
static void INT_W25Q64_Wait_Busy(void)
{
    uint32_t wait_time=HAL_GetTick();
    INT_W25Q64_Start();
    while(1)
    {
        INT_W25Q64_Write_Byte(W25Q64_READ_STATUS_REG);
        uint8_t status=INT_W25Q64_Read_Byte();
        if((status & 0x01) == 0) break;   /* S0 为 0 表示空闲，可以继续操作 */
        if(HAL_GetTick()-wait_time>3000)  /* 超时则判定芯片异常，直接跳出 */
        {
            LOG_E("[W25Q64 fail]:timeout!!!\r\n");
            break;
        }
    }
    INT_W25Q64_Stop();
}
/* 写使能 */
static void INT_W25Q64_Write_Enable(void)
{
    INT_W25Q64_Wait_Busy();
    INT_W25Q64_Start();
    INT_W25Q64_Write_Byte(W25Q64_WRITE_ENABLE);
    INT_W25Q64_Stop();
}

/* 按 块号/扇区号/页号/页内地址 写入数据 */
void INT_W25Q64_Write(uint8_t block,uint8_t sector,uint8_t page,uint8_t addr,uint8_t *data,uint16_t num)
{
    /* 把 块号 扇区号 页号 页内地址 拼接成24位起始地址
     * block每块64KB sector每扇4KB page每页256B 直接乘比移位拼接(block<<16|sector<<12|page<<8|addr)更直观*/
    uint32_t addr_24=(uint32_t)block*65536u+(uint32_t)sector*4096u+(uint32_t)page*256u+(uint32_t)addr;
    uint16_t written_len=0;

    /* 参考 INT_W25Q64_Write_with_32addr 的跨页处理逻辑：
     * W25Q64 每页 256 字节 单条 Page Program(0x02) 写入跨过页边界时 内部地址计数器会回绕到页首覆盖已写入数据
     * 必须按 256 字节页边界拆分为多次写命令 代替原先直接循环写入的错误*/
    while(written_len<num)
    {   /* 本页剩余可写字节数 = 256 - (地址 & 0xFF) */
        uint16_t page_remain=(uint16_t)(256u-((addr_24+written_len)&0xFFu));
        /* 总共剩余多少字节未写入 */
        uint16_t chunk=(uint16_t)(num-written_len);
        if(chunk>page_remain)
        {
            chunk=page_remain;   /* 单次写不超过页边界 杜绝回绕 */
        }

        /* 写使能（内部已含忙等待） */
        INT_W25Q64_Write_Enable(); 
        /* 起始位：拉低片选 */
        INT_W25Q64_Start();
        /* 发送写指令 */
        INT_W25Q64_Write_Byte(W25Q64_WRITE_DATA);
        /* 将当前24位地址切分成3个8位地址 发送给W25Q64 */
        INT_W25Q64_Write_Byte((uint8_t)(((addr_24+written_len)>>16)&0xff));
        INT_W25Q64_Write_Byte((uint8_t)(((addr_24+written_len)>>8)&0xff));
        INT_W25Q64_Write_Byte((uint8_t)((addr_24+written_len)&0xff));
        /* 发送本页这段数据 */
        for(uint16_t i=0;i<chunk;i++)
        {
            INT_W25Q64_Write_Byte(data[written_len+i]);
        }
        /* 拉高片选 结束位 */
        INT_W25Q64_Stop();

        written_len=(uint16_t)(written_len+chunk);
    }
}
/* 与 INT_W25Q64_Write 功能相同，但直接传入 32 位地址，无需拆分块/扇区/页 */
void INT_W25Q64_Write_with_32addr(uint32_t addr,uint8_t *data,uint16_t num)
{
    uint16_t written_len = 0;

    /* W25Q64 每页 256 字节：单条 Page Program(0x02) 写入跨过页边界，内部地址计数器会回绕到页首覆盖已写入数据。
     * 必须按 256 字节页边界拆分为多次写命令，代替原先直接循环写入的错误*/
    while (written_len < num)
    {   /* 本页剩余可写字节数 = 256 - (addr & 0xFF) */
        uint16_t page_remain = (uint16_t)(256u - (addr & 0xFFu));
        /* 总共剩余多少字节未写入 */
        uint16_t chunk = (uint16_t)(num - written_len);
        if (chunk > page_remain)
        {
            chunk = page_remain;   /* 单次写不超过页边界，杜绝回绕 */
        }

        /* 写使能（内部已含忙等待） */
        INT_W25Q64_Write_Enable();
        /* 拉低片选 起始位 */
        INT_W25Q64_Start();
        /* 发送写指令 */
        INT_W25Q64_Write_Byte(W25Q64_WRITE_DATA);
        /* 发送 24 位地址 */
        INT_W25Q64_Write_Byte((uint8_t)(((addr + written_len) >> 16) & 0xff));
        INT_W25Q64_Write_Byte((uint8_t)(((addr + written_len) >> 8) & 0xff));
        INT_W25Q64_Write_Byte((uint8_t)((addr + written_len) & 0xff));
        /* 发送本页这段数据 */
        for(uint16_t i=0;i<chunk;i++)
        {
            INT_W25Q64_Write_Byte(data[written_len + i]);
        }
        /* 拉高片选 结束位 */
        INT_W25Q64_Stop();

        written_len = (uint16_t)(written_len + chunk);
    }
}

/* 按 块号/扇区号/页号/页内地址 读取数据 */
void INT_W25Q64_Read(uint8_t block,uint8_t sector,uint8_t page,uint8_t addr,uint8_t *data,uint16_t num)
{
    /* 等待芯片退出忙状态 */
    INT_W25Q64_Wait_Busy();
    /* 拉低片选 起始位 */
    INT_W25Q64_Start();
    /* 将地址拼接成24位 */
    uint32_t addr_24=((uint32_t)block<<16|(uint32_t)sector<<12|(uint32_t)page<<8|(uint32_t)addr & 0xff);
    /* 发送读指令 */
    INT_W25Q64_Write_Byte(W25Q64_READ_DATA);
    /* 将24位地址切分成3个8位地址 发送给W25Q64 */
    INT_W25Q64_Write_Byte((addr_24>>16)&0xff);
    INT_W25Q64_Write_Byte((addr_24>>8)&0xff);
    INT_W25Q64_Write_Byte(addr_24&0xff);
    /* 循环读取数据 */
    for(int i=0;i<num;i++)
    {
        data[i]=INT_W25Q64_Read_Byte();
    }
    /* 拉高片选 结束位 */
    INT_W25Q64_Stop();
}
/* 与 INT_W25Q64_Read 功能相同，但直接传入 32 位地址 */
void INT_W25Q64_Read_with_32addr(uint32_t addr,uint8_t *data,uint16_t num)
{
    /* 等待芯片退出忙状态 */
    INT_W25Q64_Wait_Busy();
    /* 拉低片选 起始位 */
    INT_W25Q64_Start();
    /* 发送读指令 */
    INT_W25Q64_Write_Byte(W25Q64_READ_DATA);
    /* 发送 24 位地址：按高、中、低字节依次下发 */
    INT_W25Q64_Write_Byte((addr>>16)&0xff);/* 高 8 位 */
    INT_W25Q64_Write_Byte((addr>>8)&0xff);/* 中间 8 位 */
    INT_W25Q64_Write_Byte(addr&0xff);/* 低 8 位 */
    /* 循环读取数据 */
    for(uint16_t i=0;i<num;i++)
    {
        data[i]=INT_W25Q64_Read_Byte();
    }
    /* 拉高片选 结束位 */
    INT_W25Q64_Stop();
}

/* 扇区擦除：擦除最小单元为 1 个扇区（4KB），入参为块号与扇区号 */
void INT_W25Q64_Erase_Sector(uint8_t block,uint8_t sector)
{
    /* 写使能 */
    INT_W25Q64_Write_Enable();
    /* 拉低片选 起始位 */
    INT_W25Q64_Start();
    /* 将块、扇地址拼接成 24 位地址 */
    uint32_t addr_24=(uint32_t)block*65536+(uint32_t)sector*4096;/* 由块号与扇区号拼接 24 位地址（乘法拼接更直观） */
    /* 发送扇区擦除指令 */
    INT_W25Q64_Write_Byte(W25Q64_ERASE_SECTOR);
    /* 将24位地址切分成3个8位地址 发送给W25Q64 */
    INT_W25Q64_Write_Byte((addr_24>>16)&0xff);
    INT_W25Q64_Write_Byte((addr_24>>8)&0xff);
    INT_W25Q64_Write_Byte(addr_24&0xff);
    /* 拉高片选 结束位 */
    INT_W25Q64_Stop();
    HAL_Delay(50);/* 等待擦除完成 */
}

/* 读取制造商 ID 与设备 ID（JEDEC ID） */
void INT_W25Q64_Get_ID(uint8_t *mf_id,uint16_t *dev_id)
{
    /* 拉低片选 起始位 */
    INT_W25Q64_Start();
    /* 发送读取ID指令 */
    INT_W25Q64_Write_Byte(W25Q64_READ_ID);
    *mf_id=INT_W25Q64_Read_Byte();
    uint8_t high=INT_W25Q64_Read_Byte();
    uint8_t low=INT_W25Q64_Read_Byte();
    *dev_id=((uint16_t)high<<8 | low);
    /* 拉高片选 结束位 */
    INT_W25Q64_Stop();
}
