/**
 * @file    bl_packet.c
 * @brief   串口升级报文状态机 + 命令处理
 * @details 帧格式（必须与 PC 端工具 SerialUpgrader 完全一致，所有字段为大端）：
 *            request : 0xAA | opcode(1) | len(2) | payload(len) | crc16(2)
 *            response: 0x55 | opcode(1) | errcode(1) | len(2) | data(len) | crc16(2)
 *
 *          操作码：
 *            INQUERY 0x01  查询 BL 版本（子码 0）/ 每帧最大负载（子码 1）
 *            ERASE   0x81  擦除内部 Flash 区间   payload = addr(4)+size(4)
 *            PROGRAM 0x82  烧录内部 Flash       payload = addr(4)+size(4)+data(size)
 *            VERIFY  0x83  校验区间 CRC32       payload = addr(4)+size(4)+crc32(4)
 *            RESET   0x21  系统复位
 *            BOOT    0x22  校验 App 后跳转
 */

#include <string.h>
#include "bl_packet.h"
#include "crc16.h"
#include "crc32.h"
#include "usart.h"
#include "int_bootloader_skip.h"
#include "App_BootLoader.h"

#include "byteorder.h"     /* 显式字节序编解码层 */


/* ---- 帧 / 缓冲区常量 ---- */
/* 打包规则：
   请求帧（PC -> BL）：帧头(1B) + 操作码(1B) + 数据长度(2B) + 数据 + CRC16(2B)，总长 4 + payload + 2；
   响应帧（BL -> PC）：帧头(1B) + 操作码(1B) + 错误码(1B) + 数据长度(2B) + 数据 + CRC16(2B)。 */
#define PACKET_HEADER        0xAAu   /* 请求帧头字节 */
#define PACKET_RESPONSE      0x55u   /* 响应帧头字节 */
#define PROGRAM_DATA_MAX     4096u   /* 每帧最大 4KB */
#define PAYLOAD_SIZE_MAX     (PROGRAM_DATA_MAX + 8u)   /* payload = 数据 + 目标 Flash 地址/大小各 4B */
#define PACKET_SIZE_MAX      (4u + PAYLOAD_SIZE_MAX + 2u)   /* 2Byte CRC16 */

/* ---- 操作码 ---- */
typedef enum
{
    PACKET_OPCODE_INQUERY = 0x01,
    PACKET_OPCODE_ERASE   = 0x81,
    PACKET_OPCODE_PROGRAM = 0x82,
    PACKET_OPCODE_VERIFY  = 0x83,
    PACKET_OPCODE_RESET   = 0x21,
    PACKET_OPCODE_BOOT    = 0x22,
} packet_opcode_t;

/* ---- 响应错误码 ---- */
typedef enum
{
    PACKET_ERRCODE_OK       = 0,    /*!< 操作成功 */
    PACKET_ERRCODE_OPCODE   = 1,    /*!< 不支持的操作码 */
    PACKET_ERRCODE_OVERFLOW = 2,    /*!< 数据溢出 */
    PACKET_ERRCODE_TIMEOUT  = 3,    /*!< 超时 */
    PACKET_ERRCODE_FORMAT   = 4,    /*!< 帧格式错误 */
    PACKET_ERRCODE_VERIFY   = 5,    /*!< 校验失败 */
    PACKET_ERRCODE_PARAM    = 6,    /*!< 参数错误 */
    PACKET_ERRCODE_UNKNOWN  = 0xFF, /*!< 未知错误 */
} packet_errcode_t;

/* ---- 解析状态机 ---- */
typedef enum
{
    STATE_HEADER,   /*!< 帧头 */
    STATE_OPCODE,   /*!< 操作码 */
    STATE_LENGTH,   /*!< 长度 */
    STATE_PAYLOAD,  /*!< 负载 */
    STATE_CRC16,    /*!< 校验 */
} packet_state_t;

/* ---- 内部 Flash 边界（F407VET6，512KB） ---- */
#define FLASH_BASE_ADDR      0x08000000u
#define FLASH_SIZE      (512u * 1024u)
#define FLASH_END_ADDR       (FLASH_BASE_ADDR + FLASH_SIZE)
#define BL_END_ADDR     0x08008000u   /* BL 区结束 = App 起始；BL 区写保护 */


static uint8_t  packet_buffer[PACKET_SIZE_MAX];   /* 承接一帧的缓冲区 */
static uint16_t packet_index;
static packet_state_t packet_state;               /* 当前解析状态 */
static packet_opcode_t packet_opcode;             /* 当前帧操作码 */
static uint16_t packet_payload_len;               /* 当前帧负载长度 */

/* ========== F407 扇区映射（擦除前必须正确） ========== */
/* F407VET6 共 8 个扇区：16K 16K 16K 16K 64K 128K 128K 128K */
/**
 * @brief  将内部 Flash 地址映射到 F407 扇区号
 * @param  address 目标地址
 * @return 对应的 FLASH_SECTOR_x
 */
static uint32_t bl_addr_to_sector(uint32_t address)
{
    if      (address < 0x08004000u) return FLASH_SECTOR_0;   /* 16K */
    else if (address < 0x08008000u) return FLASH_SECTOR_1;   /* 16K */
    else if (address < 0x0800C000u) return FLASH_SECTOR_2;   /* 16K */
    else if (address < 0x08010000u) return FLASH_SECTOR_3;   /* 16K */
    else if (address < 0x08020000u) return FLASH_SECTOR_4;   /* 64K */
    else if (address < 0x08040000u) return FLASH_SECTOR_5;   /* 128K */
    else if (address < 0x08060000u) return FLASH_SECTOR_6;   /* 128K */
    else                             return FLASH_SECTOR_7;   /* 128K */
}

/**
 * @brief  擦除覆盖 [address, address+size) 的所有扇区
 * @param  address 起始地址
 * @param  size    擦除长度
 * @return 0 表示成功；-1 表示越界或擦除失败
 */
static int bl_flash_erase(uint32_t address, uint32_t size)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0;
    uint32_t end = address + size;
    uint32_t start_sector, end_sector;

    /* 越界或触及 BL 区 => 拒绝（保护 BL，防止变砖） */
    if (address < FLASH_BASE_ADDR || end > FLASH_END_ADDR || address < BL_END_ADDR)
    {
        return -1;
    }

    start_sector = bl_addr_to_sector(address);
    /* end-1：区间为 [address, end)，末字节地址是 end-1。
       例如起始 0、大小 100，实际只用到 0~99。 */
    end_sector   = bl_addr_to_sector(end - 1u);

    erase.TypeErase  = FLASH_TYPEERASE_SECTORS;
    erase.Banks = FLASH_BANK_1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector     = start_sector;
    erase.NbSectors  = end_sector - start_sector + 1u;   /* 扇区数 = 末扇号 - 首扇号 + 1 */

    HAL_FLASH_Unlock();
    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return -1;
    }
    HAL_FLASH_Lock();
    return 0;
}

/* ---- 逐字节烧录内部 Flash（简单可靠；可改用字写入提速） ---- */
/**
 * @brief  逐字节烧录内部 Flash
 * @param  address 目标地址
 * @param  data    源数据
 * @param  size    烧录长度
 * @return 0 表示成功；-1 表示越界或烧录失败
 */
static int bl_flash_program(uint32_t address, const uint8_t *data, uint32_t size)
{
    uint32_t i;

    if (address < FLASH_BASE_ADDR || address + size > FLASH_END_ADDR || address < BL_END_ADDR)
    {
        return -1;
    }

    HAL_FLASH_Unlock();
    for (i = 0; i < size; i++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, address + i, data[i]) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return -1;
        }
    }
    HAL_FLASH_Lock();
    return 0;
}

/* ---- 构建响应帧 ---- */
/**
 * @brief  按协议帧格式打包 BL 响应并通过串口发出
 * @param  opcode  响应对应的操作码
 * @param  errcode 错误码（0 表示成功；非 0 表示错误）
 * @param  data    响应数据（可为 NULL）
 * @param  len     响应数据长度
 * @note   帧格式：0x55 + 操作码 + 错误码 + 响应数据 + 数据长度 + CRC16。
 */
static void bl_response(packet_opcode_t opcode, packet_errcode_t errcode, const uint8_t *data, uint16_t len)
{
    uint8_t resp[5 + 256];   /* 固定包头 5 字节，最大载荷 256 字节 */
    uint16_t crc;

    resp[0] = PACKET_RESPONSE;   /* 响应包类型 */
    resp[1] = (uint8_t)opcode;   /* 响应对应的操作码 */
    resp[2] = (uint8_t)errcode;  /* 错误码：0 成功，其余为错误 */

    put_u16_be(&resp[3], len);   /* 数据长度，大端 */

    if (len > 0)
    {
        memcpy(&resp[5], data, len);   /* 响应数据拼接在包头之后 */
    }

    /* 计算 CRC16 软件校验值，随帧一起打包发送给上位机 */
    crc = crc16(resp, 5u + len);

    put_u16_be(&resp[5 + len], crc);   /* CRC16，大端 */

    bl_uart_send(resp, 7u + len);
}

/* ---- 命令处理 ---- */
/**
 * @brief  处理查询命令（INQUERY）：按子码查询 BL 版本或每帧最大负载
 */
static void handler_inquery(void)
{
    uint8_t subcode;   /* 子码：1 字节，标记要查询的内容 */

    if (packet_payload_len != 1)   /* 约定：查询命令的负载恒为 1 字节（子码） */
    {
        bl_response(PACKET_OPCODE_INQUERY, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }
    subcode = packet_buffer[4];   /* 帧头 + 操作码 + 长度之后才是数据，故取第 5 字节 */

    if (subcode == 0x00) /* 子码 0：查询 BL 版本 */
    {
        static const char ver[] = "1.0.0";   /* 当前 BL 版本 v1.0.0 */
        bl_response(PACKET_OPCODE_INQUERY, PACKET_ERRCODE_OK,
                    (const uint8_t *)ver, (uint16_t)(sizeof(ver) - 1));
    }
    else if (subcode == 0x01) /* 子码 1：查询约定的每帧最大负载（数据 + 地址/大小共 8B） */
    {
        uint8_t mtu[2];
        put_u16_be(mtu, PAYLOAD_SIZE_MAX);   /* 每帧最大负载，大端 */
        bl_response(PACKET_OPCODE_INQUERY, PACKET_ERRCODE_OK, mtu, 2);
    }
    else
    {
        bl_response(PACKET_OPCODE_INQUERY, PACKET_ERRCODE_PARAM, NULL, 0);   /* 无对应子码 */
    }
}

/**
 * @brief  处理擦除命令（ERASE）：解析负载中的地址与大小并擦除对应范围
 */
static void handler_erase(void)
{
    uint32_t address, size;

    if (packet_payload_len != 8)   /* 约定：擦除命令负载为 8B（起始地址、擦除大小各 4B 大端） */
    {
        bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }
    /* 帧头 4 字节（帧头 + 操作码 + 长度）之后才是数据 */
    address = get_u32_be(&packet_buffer[4]);
    size    = get_u32_be(&packet_buffer[8]);

    if (bl_flash_erase(address, size) != 0)
    {
        bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, NULL, 0);   /* 擦除失败 */
        return;
    }
    bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_OK, NULL, 0);   /* 操作成功 */
}

/**
 * @brief  处理烧录命令（PROGRAM）：解析地址与大小，校验长度一致后烧录
 */
static void handler_program(void)
{
    uint32_t address, size;

    if (packet_payload_len <= 8)   /* 至少烧录 1 字节：8B 为地址 + 大小 */
    {
        bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }
    address = get_u32_be(&packet_buffer[4]);
    size    = get_u32_be(&packet_buffer[8]);

    /* size 必须等于负载中真实数据长度，避免多写或少写 */
    if (size != (uint32_t)(packet_payload_len - 8))
    {
        /* 帧格式错误：长度字段与真实数据不符 */
        bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_FORMAT, NULL, 0);
        return;
    }

    if (bl_flash_program(address, &packet_buffer[12], size) != 0)
    {
        bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);   /* 烧录失败 */
        return;
    }
    bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_OK, NULL, 0);
}

/**
 * @brief  处理校验命令（VERIFY）：比较区间 CRC32 与负载中携带的期望值
 */
static void handler_verify(void)
{
    uint32_t address, size, expected, actual;

    if (packet_payload_len != 12)   /* 约定：校验命令负载为 12B（地址 + 大小 + CRC32） */
    {
        bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }
    address  = get_u32_be(&packet_buffer[4]);
    size     = get_u32_be(&packet_buffer[8]);
    expected = get_u32_be(&packet_buffer[12]);

    if (address < FLASH_BASE_ADDR || address + size > FLASH_END_ADDR)   /* 校验地址是否在 Flash 范围内 */
    {
        bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    actual = crc32((const uint8_t *)address, size);
    if (actual != expected)
    {
        bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_VERIFY, NULL, 0);   /* 校验错误 */
        return;
    }
    bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_OK, NULL, 0);
}

/**
 * @brief  处理复位命令（RESET）：应答后复位系统
 */
static void handler_reset(void)
{
    bl_response(PACKET_OPCODE_RESET, PACKET_ERRCODE_OK, NULL, 0);
    HAL_Delay(10);   /* 等响应发送完毕再复位 */

    bl_flags_reset_after_serial_flash();   /* 复位全部 OTA 标志 */

    NVIC_SystemReset();
}

/**
 * @brief  处理启动命令（BOOT）：应答后复位，由 BootLoader 重新走一遍启动流程
 */
static void handler_boot(void)
{
    bl_response(PACKET_OPCODE_BOOT, PACKET_ERRCODE_OK, NULL, 0);
    bl_flags_reset_after_serial_flash();   /* 复位全部 OTA 标志 */

    /* 不直接跳转：串口重新下载的固件应重新走一遍复位逻辑，做二次校验。 */
    NVIC_SystemReset();
}

/**
 * @brief  按当前帧操作码分发到对应处理函数
 */
static void bl_packet_dispatch(void)
{
    switch (packet_opcode)
    {
        case PACKET_OPCODE_INQUERY: handler_inquery(); break;
        case PACKET_OPCODE_ERASE:   handler_erase();   break;
        case PACKET_OPCODE_PROGRAM: handler_program(); break;
        case PACKET_OPCODE_VERIFY:  handler_verify();  break;
        case PACKET_OPCODE_RESET:   handler_reset();   break;
        case PACKET_OPCODE_BOOT:    handler_boot();    break;
        default:
            bl_response((packet_opcode_t)packet_buffer[1], PACKET_ERRCODE_OPCODE, NULL, 0);   /* 未知操作码 */
            break;
    }
}

/* ---- 状态机：逐字节解析一帧 ---- */
/**
 * @brief  初始化报文解析状态机（进入 BL 时调用一次）
 */
void bl_packet_init(void)
{
    packet_index = 0;
    packet_state = STATE_HEADER;
}

/**
 * @brief  核心状态机：逐字节解析一帧
 * @param  byte 从串口收到的 1 个字节
 * @return 1 表示刚完成一帧的解析与处理；0 表示仍在接收中
 * @note   状态标志只在状态机内部修改；外部仅将其置为 STATE_HEADER，
 *         因此收到的首个字节必定先做帧头判断。
 */
int bl_packet_feed(uint8_t byte)
{
    packet_buffer[packet_index++] = byte;   /* 先取索引再自增 */

    switch (packet_state)
    {
        case STATE_HEADER:
            if (byte == PACKET_HEADER)   /* 首个字节为约定帧头则进入下一状态 */
            {
                packet_state = STATE_OPCODE;
            }
            else
            {
                packet_index = 0; /* 不是帧头，继续等待 */
            }
            break;

        case STATE_OPCODE:   /* 帧头正确，接收操作码 */
            packet_opcode = (packet_opcode_t)byte;
            packet_state = STATE_LENGTH;
            break;

        case STATE_LENGTH:   /* 接收数据长度 */
            if (packet_index == 4)   /* 长度位于 buf[2]、buf[3]，收满 buf[3] 后再判断 */
            {
                packet_payload_len = get_u16_be(&packet_buffer[2]);   /* 数据长度，大端 */
                if (packet_payload_len <= PAYLOAD_SIZE_MAX)   /* 数据长度未越界 */
                {
                    packet_state = (packet_payload_len > 0) ? STATE_PAYLOAD : STATE_CRC16;
                }
                else   /* 数据长度越界，重置状态机 */
                {
                    packet_index = 0;
                    packet_state = STATE_HEADER;
                }
            }
            break;

        case STATE_PAYLOAD:   /* 收满约定长度后进入校验状态 */
            if (packet_index == 4u + packet_payload_len)
            {
                packet_state = STATE_CRC16;
            }
            break;

        case STATE_CRC16:   /* CRC16 占 2B，收满后比对 */
            if (packet_index == 4u + packet_payload_len + 2u)
            {
                uint16_t crc_recv = get_u16_be(&packet_buffer[4 + packet_payload_len]);   /* 收到的 CRC，大端 */
                uint16_t crc_calc = crc16(packet_buffer, 4u + packet_payload_len);        /* 本地重算的 CRC */

                packet_index = 0;
                packet_state = STATE_HEADER;   /* 先重置状态机，无论校验是否通过都将重新接收 */

                if (crc_recv == crc_calc)
                {
                    bl_packet_dispatch();   /* 校验通过：执行本帧对应操作 */
                    return 1;               /* 当前帧处理完成 */
                }
            }
            break;   /* CRC 校验失败则丢弃该帧，返回 0 */

        default:
            packet_index = 0;
            packet_state = STATE_HEADER;
            break;
    }

    return 0;   /* 处理失败，返回 0 */
}
