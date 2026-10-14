/**
 * @file    App_BootLoader.h
 * @brief   BootLoader 主状态机入口
 * @details 上电后由 main() 调用一次，负责完成“判断 -> 升级 / 回滚 -> 跳转 App”。
 */
#ifndef __APP_BOOTLOADER_H__
#define __APP_BOOTLOADER_H__

/** @brief 初始化 BootLoader 底层状态（标志位、串口协议状态机、串口接收中断） */
void App_BootLoader_init(void);

/** @brief BootLoader 主流程（正常情况下不会返回） */
void App_BootLoader_main(void);

/** @brief 串口烧录完成后复位全部 OTA 状态（实现见 App_BootLoader.c） */
void bl_flags_reset_after_serial_flash(void);

#endif /* __APP_BOOTLOADER_H__ */
