/**
 * @file    App_BootLoader.c
 * @brief   BootLoader 主状态机：决策 / 升级 / 回滚 / 跳转
 * @details 上电后由 App_BootLoader_main() 按序决策，任一环节失败均回退到串口恢复模式：
 *            - 按键长按         -> 串口恢复模式
 *            - 需升级标志置位   -> 校验 XBIN 头部 -> 备份 -> 擦除 -> 拷贝 -> 回读 CRC
 *                                  成功：清标志 / 失败：回滚
 *            - 启动次数超限     -> 回滚旧固件
 *            - App 区 XBIN 有效 -> 启动次数 +1 -> 跳转 App
 * @note    关键设计约束：
 *            - 头部校验失败即返回，绝不改动标志位与 App 区；
 *            - 跳转地址取自 App 的 XBIN 头部，而非写死常量；
 *            - W25Q64 数据一律经读接口读入 RAM，绝不作为指针直接解引用；
 *            - 升级标志存于 EEPROM 配置区（magic 头 + CRC16 校验），
 *              取代掉电即失的 BKUP 备份寄存器。
 * @par 修改日志:
 *            - 2026.09.19 按键检测改为 BIOS 式非阻塞窗口；
 *            - 2026.09.22 升级标志由 BKUP 寄存器迁移至 EEPROM 配置区。
 */

#include <string.h>
#include "usart.h"
#include "rtc.h"
#include "crc32.h"
#include "Int_W25Q64.h"
#include "bl_packet.h"
#include "App_BootLoader.h"
#include "magic_header.h"
#include "Int_bootloader_skip.h"
#include "byteorder.h"
/* EEPROM 配置区 + AT24C02 驱动 */
#include "eeprom_flag_header.h"
#include "Int_AT24C02.h"


/* ================= 分区地址 ================= */
#define BL_ADDR          0x08000000u    /* BootLoader 起始地址 */
#define BL_SIZE          (32u * 1024u)  /* BootLoader 区大小 32KB（sector0 + sector1） */
#define APP_A_ADDR       0x08008000u    /* App A 槽位（第一阶段唯一 App 区） */
#define APP_A_SIZE       (480u * 1024u) /* App 区总大小 480KB */
#define APP_B_ADDR       0x08040000u    /* App B 槽位（第二阶段 A/B 分区） */

/* W25Q64 暂存 / 备份地址 */
#define SPI_STAGE_ADDR   0x001000u      /* OTA 新固件暂存区（块 0 扇 1） */
#define SPI_BACKUP_ADDR  0x200000u      /* 当前 App 备份区（块 32 扇 0） */
#define DATA_PER_LEN     1024           /* 分块读写粒度 1KB */

/* 升级标志与计数上限的定义见 eeprom_flag_header.h：
   EEP_OTA_UPDATE_NEED/NONE、EEP_OTA_BOOT_COUNT_MAX/FAIL_COUNT_MAX */

#define BOOT_WINDOW_MS  2000u   /* 上电后按键检测窗口时长（2s） */
#define KEY_HOLD_MS     1000u   /* 持续按住判定为“长按”的阈值（1s） */

static void bl_backup_app_to_spi(void);
static void bl_rollback_from_spi(void);
static void bl_copy_stage_to_flash(uint32_t flash_addr, uint32_t spi_addr, uint32_t len);

static uint8_t data_buff[DATA_PER_LEN];  /* 分块读写缓冲（1KB） */
uint32_t current_app_len = APP_A_SIZE;   /* 当前旧固件的实际长度 */
extern uint8_t rx_data;                  /* 串口接收字节（usart.c 中定义） */

/* ================= 标志位：EEPROM 配置区封装 ================= */

/**
 * @brief  计算 EEPROM 配置区标志的 CRC16（poly 0x1021，init 0x0000）
 * @param  data 待校验数据首地址
 * @param  len  数据长度（配置区固定为 14）
 * @return 16 位 CRC 值
 * @note   与串口协议帧的 CRC16 分离实现：本处仅 14 字节，无需查表法，
 *         可省下查表所需的 512B RAM。
 */
static uint16_t bl_flag_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0x0000u;
    uint16_t i; int j;
    for (i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x8000u) crc = (uint16_t)((crc << 1) ^ 0x1021u);
            else               crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/**
 * @brief  读取 EEPROM 配置区的 14 字节标志并校验 magic + CRC
 * @param  buf 输出缓冲区，长度固定为 14 字节
 * @return 0 表示标志有效；-1 表示 magic 或 CRC 损坏
 */
static int bl_flag_read_all(uint8_t buf[14])
{
    uint8_t crc[2];
    INT_AT24C02_Read_Bytes(EEP_OTA_MAGIC, buf, 14);
    INT_AT24C02_Read_Bytes(EEP_OTA_CRC_H, crc, 2);
    if (buf[0] != EEP_OTA_MAGIC_VAL) return -1;
    uint16_t expect_crc = get_u16_be(crc);
    uint16_t calc_crc   = bl_flag_crc16(buf, 14);   /* 本地重算 CRC 用于比对 */
    return (calc_crc == expect_crc) ? 0 : -1;
}

/**
 * @brief  重算配置区 CRC 并写回 EEPROM（14 字节标志 + 2 字节 CRC）
 * @param  buf 调整后的 14 字节标志数组
 */
static void bl_flag_revise(uint8_t buf[14])
{
    uint16_t crc = bl_flag_crc16(buf, 14);
    uint8_t c[2];
    put_u16_be(c, crc);
    INT_AT24C02_Write_Bytes(EEP_OTA_MAGIC, buf, 14);   /* 写标志 */
    INT_AT24C02_Write_Bytes(EEP_OTA_CRC_H, c, 2);      /* 写 CRC */
}


/**
 * @brief  读取指定位置的单字节标志
 * @param  addr 标志在配置区中的偏移
 * @return 该字节标志值；配置区损坏时按默认值（全 0）处理
 */
static uint8_t bl_flag_read_u8(uint8_t addr)
{
    uint8_t buf[14] = {0};
    if (bl_flag_read_all(buf) != 0) { memset(buf, 0, 14); buf[0] = EEP_OTA_MAGIC_VAL; }
    return buf[addr];
}

/**
 * @brief  写入指定位置的单字节标志（自动重算 CRC）
 * @param  addr 标志在配置区中的偏移
 * @param  val  待写入的标志值
 */
static void bl_flag_write_u8(uint8_t addr, uint8_t val)
{
    uint8_t buf[14] = {0};
    if (bl_flag_read_all(buf) != 0) { memset(buf, 0, 14); buf[0] = EEP_OTA_MAGIC_VAL; }
    buf[addr] = val;
    bl_flag_revise(buf);
}

/**
 * @brief  读取指定位置的 4 字节大端标志（旧目标版本 / 旧固件长度）
 * @param  addr 标志在配置区中的偏移
 * @return 解析出的 32 位数值
 */
static uint32_t bl_flag_read_u32_be(uint8_t addr)
{
    uint8_t buf[14] = {0};
    if (bl_flag_read_all(buf) != 0) { memset(buf, 0, 14); buf[0] = EEP_OTA_MAGIC_VAL; }
    return get_u32_be(&buf[addr]);
}

/**
 * @brief  写入指定位置的 4 字节大端标志（自动重算 CRC）
 * @param  addr  标志在配置区中的偏移
 * @param  value 待写入的 32 位数值
 */
static void bl_flag_write_u32_be(uint8_t addr, uint32_t value)
{
    uint8_t buf[14] = {0};
    if (bl_flag_read_all(buf) != 0) { memset(buf, 0, 14); buf[0] = EEP_OTA_MAGIC_VAL; }
    put_u32_be(&buf[addr], value);
    bl_flag_revise(buf);
}

/**
 * @brief  初始化 EEPROM 标志存储
 * @note   若 magic + CRC 自检不通过（未初始化 / 数据损坏），则写入默认值：
 *         magic 置为 EEP_OTA_MAGIC_VAL，其余标志位全部清 0。
 */
static void bl_flag_init(void)
{
    uint8_t buf[14] = {0};
    if (bl_flag_read_all(buf) != 0)
    {
        memset(buf, 0, 14);
        buf[EEP_OTA_MAGIC] = EEP_OTA_MAGIC_VAL;
        bl_flag_revise(buf);
    }
}

/**
 * @brief  查询是否需要升级
 * @param  无
 * @return 1 表示需升级；0 表示无需升级
 * @note   判断配置区 0x01 处的标志是否为约定值 EEP_OTA_UPDATE_NEED。
 */
static int bl_flag_need_update(void)
{
    return (bl_flag_read_u8(EEP_OTA_UPDATE_FLAG) == EEP_OTA_UPDATE_NEED);
}

/**
 * @brief  清除升级标志（配置区 0x01 置 0）
 */
static void bl_flag_clear_update(void)
{
    bl_flag_write_u8(EEP_OTA_UPDATE_FLAG, EEP_OTA_UPDATE_NONE);
}

/**
 * @brief  获取启动计数（配置区 0x02）
 * @return 当前启动计数值
 */
static uint32_t bl_flag_boot_count(void)
{
    return bl_flag_read_u8(EEP_OTA_BOOT_COUNT);
}

/**
 * @brief  启动计数 +1（配置区 0x02）
 */
static void bl_flag_inc_boot_count(void)
{
    bl_flag_write_u8(EEP_OTA_BOOT_COUNT,(uint8_t)(bl_flag_read_u8(EEP_OTA_BOOT_COUNT) + 1u));
}

/**
 * @brief  清零启动计数（配置区 0x02）
 */
static void bl_flag_clear_boot_count(void)
{
    bl_flag_write_u8(EEP_OTA_BOOT_COUNT, 0u);
}

/**
 * @brief  升级失败计数 +1（配置区由回滚路径调用，供 App 侧判断是否停止自动升级）
 */
static void bl_flag_inc_fail_count(void)
{
    bl_flag_write_u8(EEP_OTA_FAIL_COUNT,(uint8_t)(bl_flag_read_u8(EEP_OTA_FAIL_COUNT) + 1u));
}

/**
 * @brief  复位全部 OTA 状态
 * @note   防止“刚烧录完成就被主流程第 4 步的陈旧回滚判定覆盖”。
 *         由 bl_packet.c 在串口烧录完成后调用：清升级标志 / 启动计数 /
 *         失败计数 / 旧固件长度 / 备份有效标志（attempt_ver、old_len、
 *         reserved 保持 0）。
 */
void bl_flags_reset_after_serial_flash(void)
{
    uint8_t buf[14] = {0};
    buf[EEP_OTA_MAGIC] = EEP_OTA_MAGIC_VAL;
    buf[EEP_OTA_UPDATE_FLAG] = EEP_OTA_UPDATE_NONE;
    buf[EEP_OTA_BOOT_COUNT]  = 0;
    buf[EEP_OTA_FAIL_COUNT]  = 0;
    buf[EEP_OTA_BACKUP_VALID]= 0;
    bl_flag_revise(buf);
}

/* ================= 内部 Flash 擦除（扇区映射与 bl_packet.c 一致） ================= */
/**
 * @brief  将内部 Flash 地址映射到 F407 扇区号
 * @param  address 目标地址
 * @return 对应的 FLASH_SECTOR_x
 */
static uint32_t bl_addr_to_sector(uint32_t address)
{
    if      (address < 0x08004000u) return FLASH_SECTOR_0;
    else if (address < 0x08008000u) return FLASH_SECTOR_1;
    else if (address < 0x0800C000u) return FLASH_SECTOR_2;
    else if (address < 0x08010000u) return FLASH_SECTOR_3;
    else if (address < 0x08020000u) return FLASH_SECTOR_4;
    else if (address < 0x08040000u) return FLASH_SECTOR_5;
    else if (address < 0x08060000u) return FLASH_SECTOR_6;
    else                             return FLASH_SECTOR_7;
}

/**
 * @brief  擦除内部 Flash 的 App 区
 * @param  address 起始地址
 * @param  size    擦除长度
 * @return 0 表示成功；-1 表示越界或擦除失败
 */
static int bl_erase_app(uint32_t address, uint32_t size)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0;
    uint32_t end = address + size;
    uint32_t start_sector, end_sector;

    if (address < APP_A_ADDR || end > 0x08080000u)   /* 超出 App 区范围 => 拒绝 */
    {
        return -1;
    }

    start_sector = bl_addr_to_sector(address);
    end_sector   = bl_addr_to_sector(end - 1u);
    erase.Banks = FLASH_BANK_1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.TypeErase  = FLASH_TYPEERASE_SECTORS;
    erase.Sector     = start_sector;
    erase.NbSectors  = end_sector - start_sector + 1u;

    HAL_FLASH_Unlock();
    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return -1;
    }
    HAL_FLASH_Lock();
    return 0;
}

/* ================= 校验 App 区（读取 XBIN 头部 + 负载 CRC） ================= */
/**
 * @brief  判断 App 区固件是否有效（XBIN 头部 + 机型 + 负载 CRC32）
 * @param  app_addr App 区起始地址
 * @return 1 有效；0 无效（调用方据此回滚或进入串口恢复）
 */
static int bl_app_is_valid(uint32_t app_addr)
{
    magic_header_t hdr;
    uint32_t offset,len;

    /* 1. 读取 App 区头部（App 区前 4096 字节即 XBIN 头部） */
    memcpy(&hdr, (const void *)app_addr, sizeof(hdr));

    /* 2. 头部 + 机型校验 */
    if (!magic_header_validate(&hdr))
    {
        return 0;
    }
    if (magic_header_get_hw_model(&hdr) != MAGIC_HEADER_HW_MODEL_WEATHERCLOCK_F407)
    {
        return 0;
    }

    /* 3. 先判范围再算 CRC：CRC 可能校验通过但长度越界，一旦越读 Flash 末尾
       将触发 HardFault，而此处位于 bl_enter_recovery() 之前，崩溃后无法再进入串口恢复。 */
    offset = magic_header_get_offset(&hdr);
    len = magic_header_get_length(&hdr);
    if (offset < sizeof(magic_header_t) || (uint64_t)offset + len > APP_A_SIZE)
    {
        return 0;
    }

    /* 4. 整包负载 CRC 校验 */
    if (crc32((const uint8_t *)(app_addr + magic_header_get_offset(&hdr)),
              magic_header_get_length(&hdr)) != magic_header_get_crc32(&hdr))
    {
        return 0;
    }

    return 1;
}

/* ================= 升级流程 ================= */
/**
 * @brief  获取当前旧固件的实际长度（升级前调用）
 * @note   旧固件若遵循 XBIN 协议，则长度 = 头部偏移 + 负载长度；
 *         否则无法获取，按整个 App 区长度处理。
 */
static void bl_get_current_app_len(void)
{
    magic_header_t hdr;

    memcpy(&hdr, (const void *)APP_A_ADDR, sizeof(hdr));

    /* 固件长度必须包含 XBIN 头部：只取 payload 长度会导致回滚时漏掉尾部 4KB。 */
    if (magic_header_validate(&hdr) &&
        magic_header_get_type(&hdr) == MAGIC_HEADER_TYPE_APP &&
        magic_header_get_hw_model(&hdr) == MAGIC_HEADER_HW_MODEL_WEATHERCLOCK_F407)
    {
        /* 头部有效时，data_offset 即头部占用大小 */
        uint32_t total = magic_header_get_offset(&hdr) + magic_header_get_length(&hdr);
        /* 越界保护：算不出来 / 超出 App 区 => 回退到整区 */
        current_app_len = (total > APP_A_SIZE) ? APP_A_SIZE : total;
    }
    else
    {
        current_app_len = APP_A_SIZE;   /* 头校验失败：按整个 App 区处理；此处绝不能再 +4096，否则 erase 越界 */
    }
}

/**
 * @brief  执行升级：把 W25Q64 暂存区的新固件写入内部 Flash 的 App 区
 * @note   流程：读头部 -> 校验头部 -> 备份旧固件 -> 擦除 -> 拷贝 -> 回读 CRC
 *         -> 成功：清标志 / 失败：回滚。
 *         每一步校验失败均直接返回，绝不改动标志位与 App 区。
 */
static void bl_do_upgrade(void)
{
    magic_header_t hdr;
    uint32_t len, crc_expect, crc_actual,offset;

    /* 1. 从暂存区读取 256 字节头部 */
    INT_W25Q64_Read_with_32addr(SPI_STAGE_ADDR, (uint8_t *)&hdr, sizeof(hdr));

    /* 2. 头部非法 => 丢弃该包，绝不改动 App */
    if (!magic_header_validate(&hdr))
    {
        bl_flag_clear_update();
        return;
    }
    if (magic_header_get_hw_model(&hdr) != MAGIC_HEADER_HW_MODEL_WEATHERCLOCK_F407)
    {
        bl_flag_clear_update();
        return;
    }
    if (magic_header_get_type(&hdr) != MAGIC_HEADER_TYPE_APP)
    {
        bl_flag_clear_update();
        return;
    }

    len        = magic_header_get_length(&hdr);
    crc_expect = magic_header_get_crc32(&hdr);

    /* 统一用头部声明的偏移量，不用硬编码 4096 */
    offset = magic_header_get_offset(&hdr);
    /* offset 至少要比结构体大（防“长度 0/偏移 0”的合法空镜像），总量不超 App 区 */
    if (offset < sizeof(magic_header_t) || (uint64_t)offset + len > APP_A_SIZE)
    {
        bl_flag_clear_update();
        return;
    }
    /* 3. 先备份当前 App 到 W25Q64 备份区（失败时从此处恢复） */
    bl_backup_app_to_spi();

    /* 4. 擦除 App 区 */
    if (bl_erase_app(APP_A_ADDR, len+offset) != 0)
    {
        bl_flag_clear_update();   /* 擦除失败：清除更新标志 */
        return;
    }
    /* 5. 分块拷贝：从暂存区读取负载和新固件头信息。 */
    bl_copy_stage_to_flash(APP_A_ADDR, SPI_STAGE_ADDR ,len+offset);

    /* 6. 回读校验：新固件 CRC 必须与头部一致，否则回滚（bug16 教训） */
    /* 本地 CRC 计算基于“头起始 + 偏移”，而非依赖头部内声明的 app_addr */
    crc_actual = crc32((const uint8_t *)(APP_A_ADDR + offset), len);
    if (crc_actual != crc_expect)
    {
        bl_rollback_from_spi();   /* 校验失败：取消升级，从备份恢复旧固件并清除标志位 */
        return;
    }

    /* 7. 成功：清除需升级标志 + 清零启动计数（重新开始计数） */
    bl_flag_clear_update();
    bl_flag_clear_boot_count();
}

/* ================= 回滚 / 备份 ================= */
/**
 * @brief  分块将数据写入内部 Flash（按字写入，末尾不足 1 字按字节写入）
 * @param  flash_addr 内部 Flash 目标地址
 * @param  spi_addr   W25Q64 源地址
 * @param  len        写入长度
 */
static void write_flash_patch_by_patch(uint32_t flash_addr,uint32_t spi_addr,uint32_t len)
{
    HAL_FLASH_Unlock();
    uint32_t remain_data_len=len;   /* 剩余待写入数据量 */
    uint32_t had_written_len=0;     /* 已写入数据量 */
    while(remain_data_len>0)
    {
        /* 分块传输：判断本次是否为最后一块，据此决定本次写入的字节数 */
        uint32_t write_len=remain_data_len>DATA_PER_LEN?DATA_PER_LEN:remain_data_len;
        /* 从暂存区读取数据 */
        INT_W25Q64_Read_with_32addr(spi_addr+had_written_len, data_buff, write_len);
        uint32_t i=0;
        /* 循环条件须为 i+4<=write_len：若写成 i<write_len，末尾不足 4 字节时会越界 */
        for(;i+4<=write_len;i+=4)
        {
            /* 内部 Flash 按小端存储（低位在前） */
            uint32_t data32 = get_u32_le(&data_buff[i]);
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flash_addr+had_written_len+i, data32);
        }
        for(;i<write_len;i++)   /* 不足 1 字，按字节写入 */
        {
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, flash_addr+had_written_len+i, data_buff[i]);
        }
        remain_data_len-=write_len;   /* 剩余数据量递减 */
        had_written_len+=write_len;   /* 已写入量递增 */
    }
    HAL_FLASH_Lock();
}

/**
 * @brief  备份当前 App 到 W25Q64 备份区（升级失败时从此处恢复）
 * @note   备份成功后记录旧固件长度并置“备份有效”标志。
 */
static void bl_backup_app_to_spi(void)
{
    memset(data_buff,0,DATA_PER_LEN);   /* 清掉缓冲区中的脏数据 */
    /* 1. 按扇擦除备份区 */
    for (uint32_t a = 0; a < current_app_len; a += 4096u)
    {
        uint32_t addr = SPI_BACKUP_ADDR + a;
        /* W25Q64 地址译码：addr/65536 为块号；addr%65536 取低 16 位，再 >>12 得扇号 */
        INT_W25Q64_Erase_Sector((uint8_t)(addr / 65536u),
                                (uint8_t)((addr % 65536u) / 4096u));
    }
    /* 2. 分块读取内部 Flash 的 App 区（RAM 有限，无法一次性放下整个 App），
          并逐块写入 W25Q64 备份区 */
    /* bug：proc_times 向下取整会漏掉最后一轮，故此处用 while 按剩余长度推进 */
    uint32_t had_written_len=0;
    while(had_written_len<current_app_len)
    {
        uint32_t write_len=current_app_len-had_written_len;
        if(write_len>DATA_PER_LEN) {write_len=DATA_PER_LEN;}   /* 超出 1KB 则截断为一块 */
        memcpy(data_buff,(const uint8_t *)APP_A_ADDR+had_written_len,write_len);
        INT_W25Q64_Write_with_32addr(SPI_BACKUP_ADDR+had_written_len,data_buff,(uint16_t)write_len);
        had_written_len+=write_len;
    }
    bl_flag_write_u32_be(EEP_OTA_OLD_LEN, current_app_len);   /* 旧固件长度存入 EEPROM */
    bl_flag_write_u8(EEP_OTA_BACKUP_VALID, EEP_OTA_BACKUP_VALID_VAL);   /* 置“备份有效”标志 */
}

/**
 * @brief  回滚：取消升级，把旧固件从备份区恢复到 App 区并清除标志位
 * @note   进入回滚即视为一次升级失败，失败计数 +1。
 */
static void bl_rollback_from_spi(void)
{
    bl_flag_inc_fail_count();   /* 进入回滚即视为一次升级失败，失败计数 +1 */
	printf("[Bootloader]Update fail,rollback\r\n");
    memset(data_buff,0,DATA_PER_LEN);   /* 清掉缓冲区中的脏数据 */

    uint32_t restore_len = bl_flag_read_u32_be(EEP_OTA_OLD_LEN);

    if (bl_flag_read_u8(EEP_OTA_BACKUP_VALID) != EEP_OTA_BACKUP_VALID_VAL ||
        restore_len == 0u || restore_len > APP_A_SIZE)
    {
        /* 从未备份过 / 记录非法：不要把整区脏数据写回，直接放弃本次回滚 */
        printf("[BL]rollback aborted: no valid backup\r\n");
        bl_flag_clear_update();
        bl_flag_clear_boot_count();
        return;
    }
    /* 1. 擦除足够大小的 Flash */
    if(bl_erase_app(APP_A_ADDR,restore_len)!=0)
    {
        printf("bl_rollback_from_spi:erase_app fail\r\n");
        return;
    }
    /* 2. 分块读取 W25Q64 备份区并写入 Flash */
    write_flash_patch_by_patch(APP_A_ADDR, SPI_BACKUP_ADDR, restore_len);

    bl_flag_clear_update();
    bl_flag_clear_boot_count();
}

/**
 * @brief  分块拷贝：从暂存区读取负载并写入内部 Flash
 * @param  flash_addr 内部 Flash 目标地址
 * @param  spi_addr   W25Q64 源地址
 * @param  len        拷贝长度
 */
static void bl_copy_stage_to_flash(uint32_t flash_addr, uint32_t spi_addr, uint32_t len)
{
    memset(data_buff,0,DATA_PER_LEN);   /* 清掉缓冲区中的脏数据 */
    write_flash_patch_by_patch(flash_addr,spi_addr,len);
}

/* ================= 串口恢复模式（兜底） ================= */
/* 按键检测用轮询而非中断的原因：轮询若实现不当，容易先进入更新模式、或被更新流程阻塞，
   长按事件被吞掉；改用窗口期内非阻塞轮询更直接可控。 */

/**
 * @brief  BIOS 式非阻塞长按检测（2026.09.19 改造）
 * @return true 表示检测到长按，调用方据此进入串口烧录模式；false 表示未长按
 * @note   上电后 BOOT_WINDOW_MS(2s) 窗口内，持续按住 KEY2 超过 KEY_HOLD_MS(1s)
 *         判定为长按；期间主循环不阻塞，可并行执行预读标志 / 打印提示等操作。
 *         按下为低电平（KEY2 配为 GPIO_PULLUP 上拉），长按计时本身隐含消抖。
 */
static bool bl_wait_key_hold(void)
{
    uint32_t win_start   = HAL_GetTick();   /* 窗口起点 */
    uint32_t press_start = 0;               /* 本次按下的起始时刻（0 表示未按下） */
    bool pressed = false;                   /* 当前是否处于按下状态 */

    /* 窗口期内并行打印一次提示（提示用户此时可长按进入升级模式） */
    printf("[BL] hold KEY2 1s to enter serial upgrade mode\r\n");

    while (HAL_GetTick() - win_start < BOOT_WINDOW_MS)
    {
        if (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_RESET)
        {
            /* 第一次按下（低电平） */
            if (!pressed)
            {
                pressed = true;
                press_start = HAL_GetTick();   /* 记录按下起始时刻 */
            }
            else if (HAL_GetTick() - press_start >= KEY_HOLD_MS)
            {
                return true;                    /* 持续按住超过阈值 => 长按确认 */
            }
        }
        else
        {
            pressed = false;                    /* 松开，清零重新计时（防误触） */
        }
    }
    return false;                              /* 窗口结束未长按 */
}

/**
 * @brief  进入串口恢复模式（BIOS 长按触发，兜底通道）
 * @note   死循环从串口接收端取字节并喂给报文状态机，
 *         PC 工具通过 XBIN 协议将固件直接烧录到 Flash。本函数正常情况下不会返回。
 */
static void bl_enter_recovery(void)
{
	printf("[BootLoader] enter recovery\r\n");
    for (;;)
    {
        uint8_t byte;
        if (bl_uart_read_byte(&byte))
        {
            bl_packet_feed(byte);
        }
    }
}

/* ================= 主流程 ================= */
/**
 * @brief  初始化 BootLoader 底层状态
 * @note   必须在主流程之前调用。
 */
void App_BootLoader_init(void)
{
    bl_flag_init();          /* 标志位访问（必须最先执行） */
    bl_packet_init();        /* 串口协议状态机 */
    HAL_UART_Receive_IT(&huart1,&rx_data,1);   /* 使能接收中断 */
}

/**
 * @brief  BootLoader 主流程（正常情况下不会返回）
 * @note   顺序：取旧固件长度 -> 按键长按判定 -> 升级 -> 启动次数超限回滚
 *         -> App 有效则跳转 -> 兜底进入串口恢复模式。
 */
void App_BootLoader_main(void)
{
    bl_get_current_app_len();   /* 获取当前旧固件的实际长度 */

    /* ---- 2. 按键长按 -> 强制进入串口恢复（BIOS 式非阻塞窗口，2026.9.19 改造） ---- */
    int need_update;                       /* 预读的升级标志 */
    need_update = bl_flag_need_update();   /* 窗口期内预读升级标志（稳定值，供后续分支复用） */
    if (bl_wait_key_hold())                /* 2s 窗口内持续长按 >= 1s => 进入串口烧录 */
    {
        bl_enter_recovery();
    }

    /* 3. 需升级标志 -> 执行升级（内含校验/备份/回滚） */
    if (need_update)                       /* 使用窗口期预读的标志 */
    {
        bl_do_upgrade();
    }

    /* 4. 启动次数超限 -> 新固件启动失败，执行回滚
       保持升级后读取，避免升级清计数后被旧值误判回滚 */
    if (bl_flag_boot_count() >= EEP_OTA_BOOT_COUNT_MAX)
    {
        bl_rollback_from_spi();
    }

    /* 5. App 区有效 -> 启动次数+1 -> 跳转 */
    if (bl_app_is_valid(APP_A_ADDR))
    {
        bl_flag_inc_boot_count();
		printf("[BL]jumping to app...\r\n");
        Int_bootloader_jump_to_app(APP_START_ADDR);
    }

    /* 6. 兜底：进入串口恢复，等待 PC 烧录 */
    bl_enter_recovery();
}
