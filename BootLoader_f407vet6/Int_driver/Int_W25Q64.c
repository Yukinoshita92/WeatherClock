/**
 * @file    Int_W25Q64.c
 * @brief   W25Q64 SPI Flash 驱动实现（64Mbit / 8MByte）
 * @note    - HAL 库配置完成后外设已初始化，本文件只封装读写函数，无需手动初始化；
 *          - 读数据 / 读 ID / 读状态寄存器无需写使能；写数据、写状态寄存器、擦除均需先写使能；
 *          - W25Q64 地址为 24 位：块 8bit（最高位恒为 0）、扇区 4bit、页 4bit、页内地址 8bit；
 *          - 驱动端已处理跨页覆写：两个写入接口内部都会按 256 字节页边界拆分为多次页写，
 *            应用层可一次性传入任意长度数据。
 */

#include "Int_W25Q64.h"

/** @brief 片选拉低（SPI 起始位），CS 接 PD6 */
static void INT_W25Q64_Start(void)
{
    HAL_GPIO_WritePin(W25Q64_CS_GPIO_Port, W25Q64_CS_Pin, GPIO_PIN_RESET);
}

/** @brief 片选拉高（SPI 结束位），CS 接 PD6 */
static void INT_W25Q64_Stop(void)
{
    HAL_GPIO_WritePin(W25Q64_CS_GPIO_Port, W25Q64_CS_Pin, GPIO_PIN_SET);
}

/** @brief 通过 SPI 写入 1 字节 */
static void INT_W25Q64_Write_Byte(uint8_t data)
{
    HAL_SPI_Transmit(&hspi1,&data,1,100);
}

/** @brief 通过 SPI 读取 1 字节 */
static uint8_t INT_W25Q64_Read_Byte(void)
{
    uint8_t data;
    HAL_SPI_Receive(&hspi1,&data,1,100);
    return data;
}

/**
 * @brief  轮询等待器件空闲（读状态寄存器 S0 忙位）
 * @note   超时 3s 视为器件异常，打印提示后退出，避免死等。
 */
static void INT_W25Q64_Wait_Busy(void)
{
    uint32_t wait_time=HAL_GetTick();
    INT_W25Q64_Start();
    while(1)
    {
        INT_W25Q64_Write_Byte(W25Q64_READ_STATUS_REG);
        uint8_t status=INT_W25Q64_Read_Byte();
        if((status & 0x01) == 0) break;   /* S0 = 0 表示空闲 */
        if(HAL_GetTick()-wait_time>3000)
        {
            printf("[W25Q64 fail]:timeout!!!\r\n");
            break;
        }
    }
    INT_W25Q64_Stop();
}

/**
 * @brief  发送写使能指令（内部先做忙等待）
 */
static void INT_W25Q64_Write_Enable(void)
{
    INT_W25Q64_Wait_Busy();
    INT_W25Q64_Start();
    INT_W25Q64_Write_Byte(W25Q64_WRITE_ENABLE);
    INT_W25Q64_Stop();
}

/**
 * @brief  将数据写入 W25Q64（按块/扇/页/页内地址定位）
 * @param  block  块号
 * @param  sector 扇区号
 * @param  page   页号
 * @param  addr   页内地址
 * @param  data   数据指针
 * @param  num    数据长度
 * @note   跨页覆写：单条 Page Program(0x02) 写入跨过 256 字节页边界时，器件内部地址
 *         计数器会回绕到页首并覆盖已写入数据，故必须按页边界拆分多次写命令。
 */
void INT_W25Q64_Write(uint8_t block,uint8_t sector,uint8_t page,uint8_t addr,uint8_t *data,uint16_t num)
{
    /* 块/扇/页/页内地址拼接成 24 位起始地址：
       block 每块 64KB、sector 每扇 4KB、page 每页 256B，直接相乘比移位拼接更直观 */
    uint32_t addr_24=(uint32_t)block*65536u+(uint32_t)sector*4096u+(uint32_t)page*256u+(uint32_t)addr;
    uint16_t written_len=0;

    while(written_len<num)
    {
        /* 本页剩余可写字节：addr&0xFF 取页内偏移，256 减去偏移即本页剩余空间 */
        uint16_t page_remain=(uint16_t)(256u-((addr_24+written_len)&0xFFu));
        /* 总共剩余多少字节未写入 */
        uint16_t chunk=(uint16_t)(num-written_len);
        if(chunk>page_remain)
        {
            chunk=page_remain;   /* 单次写不超过页边界 杜绝回绕 */
        }

        INT_W25Q64_Write_Enable();
        INT_W25Q64_Start();
        INT_W25Q64_Write_Byte(W25Q64_WRITE_DATA);
        INT_W25Q64_Write_Byte((uint8_t)(((addr_24+written_len)>>16)&0xff));
        INT_W25Q64_Write_Byte((uint8_t)(((addr_24+written_len)>>8)&0xff));
        INT_W25Q64_Write_Byte((uint8_t)((addr_24+written_len)&0xff));
        for(uint16_t i=0;i<chunk;i++)
        {
            INT_W25Q64_Write_Byte(data[written_len+i]);
        }
        INT_W25Q64_Stop();

        written_len=(uint16_t)(written_len+chunk);
    }
}

/**
 * @brief  将数据写入 W25Q64（直接传入 32 位地址）
 * @param  addr 24 位存储地址
 * @param  data 数据指针
 * @param  num  数据长度
 * @note   跨页覆写处理同 INT_W25Q64_Write：按 256 字节页边界拆分多次写命令。
 */
void INT_W25Q64_Write_with_32addr(uint32_t addr,uint8_t *data,uint16_t num)
{
    uint16_t written_len = 0;

    while (written_len < num)
    {
        /* 本页剩余可写字节：addr&0xFF 取页内偏移，256 减去偏移即本页剩余空间 */
        uint16_t page_remain = (uint16_t)(256u - (addr & 0xFFu));
        /* 总共剩余多少字节未写入 */
        uint16_t chunk = (uint16_t)(num - written_len);
        if (chunk > page_remain)
        {
            chunk = page_remain;   /* 单次写不超过页边界，杜绝回绕 */
        }

        INT_W25Q64_Write_Enable();
        INT_W25Q64_Start();
        INT_W25Q64_Write_Byte(W25Q64_WRITE_DATA);
        INT_W25Q64_Write_Byte((uint8_t)(((addr + written_len) >> 16) & 0xff));
        INT_W25Q64_Write_Byte((uint8_t)(((addr + written_len) >> 8) & 0xff));
        INT_W25Q64_Write_Byte((uint8_t)((addr + written_len) & 0xff));
        for(uint16_t i=0;i<chunk;i++)
        {
            INT_W25Q64_Write_Byte(data[written_len + i]);
        }
        INT_W25Q64_Stop();

        written_len = (uint16_t)(written_len + chunk);
    }
}


/**
 * @brief  从 W25Q64 读取数据（按块/扇/页/页内地址定位）
 * @param  block  块号
 * @param  sector 扇区号
 * @param  page   页号
 * @param  addr   页内地址
 * @param  data   接收缓冲区
 * @param  num    读取长度
 */
void INT_W25Q64_Read(uint8_t block,uint8_t sector,uint8_t page,uint8_t addr,uint8_t *data,uint16_t num)
{

    INT_W25Q64_Wait_Busy();
    INT_W25Q64_Start();
    uint32_t addr_24=((uint32_t)block<<16|(uint32_t)sector<<12|(uint32_t)page<<8|(uint32_t)addr & 0xff);   /* 拼接为 24 位地址 */
    INT_W25Q64_Write_Byte(W25Q64_READ_DATA);
    INT_W25Q64_Write_Byte((addr_24>>16)&0xff);
    INT_W25Q64_Write_Byte((addr_24>>8)&0xff);
    INT_W25Q64_Write_Byte(addr_24&0xff);
    for(int i=0;i<num;i++)
    {
        data[i]=INT_W25Q64_Read_Byte();
    }
    INT_W25Q64_Stop();
}

/**
 * @brief  从 W25Q64 读取数据（直接传入 32 位地址）
 * @param  addr 24 位存储地址
 * @param  data 接收缓冲区
 * @param  num  读取长度
 */
void INT_W25Q64_Read_with_32addr(uint32_t addr,uint8_t *data,uint16_t num)
{
    INT_W25Q64_Wait_Busy();
    INT_W25Q64_Start();
    INT_W25Q64_Write_Byte(W25Q64_READ_DATA);
    /* 24 位地址分 3 个字节由高到低发送 */
    INT_W25Q64_Write_Byte((addr>>16)&0xff);
    INT_W25Q64_Write_Byte((addr>>8)&0xff);
    INT_W25Q64_Write_Byte(addr&0xff);
    for(uint16_t i=0;i<num;i++)
    {
        data[i]=INT_W25Q64_Read_Byte();
    }
    INT_W25Q64_Stop();
}

/**
 * @brief  擦除一个扇区（最小擦除单元 1 扇 = 4KB）
 * @param  block  块号
 * @param  sector 扇区号
 */
void INT_W25Q64_Erase_Sector(uint8_t block,uint8_t sector)
{
    INT_W25Q64_Write_Enable();
    INT_W25Q64_Start();
    /* 用乘法拼接块/扇地址（移位写法此前出现过 bug，此处直接相乘更可靠） */
    uint32_t addr_24=(uint32_t)block*65536+(uint32_t)sector*4096;
    INT_W25Q64_Write_Byte(W25Q64_ERASE_SECTOR);
    INT_W25Q64_Write_Byte((addr_24>>16)&0xff);
    INT_W25Q64_Write_Byte((addr_24>>8)&0xff);
    INT_W25Q64_Write_Byte(addr_24&0xff);
    INT_W25Q64_Stop();
}

/**
 * @brief  读取 W25Q64 的制造商 ID 和设备 ID
 * @param  mf_id  输出：制造商 ID
 * @param  dev_id 输出：设备 ID
 */
void INT_W25Q64_Get_ID(uint8_t *mf_id,uint16_t *dev_id)
{
    INT_W25Q64_Start();
    INT_W25Q64_Write_Byte(W25Q64_READ_ID);
    *mf_id=INT_W25Q64_Read_Byte();
    uint8_t high=INT_W25Q64_Read_Byte();
    uint8_t low=INT_W25Q64_Read_Byte();
    *dev_id=((uint16_t)high<<8 | low);
    INT_W25Q64_Stop();
}
