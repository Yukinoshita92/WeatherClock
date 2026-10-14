/**
 * @file    esp_at_ota.c
 * @brief   ESP 透传下载（OTA 专用）：TCP 建链 -> HTTP GET -> 字节流读取
 * @details 透传模式下 ESP 不再解析 AT 指令，TCP 收到的字节原样直通 USART2；串口中断
 *          把字节写入本文件的环形缓冲，任务侧由 esp_at_ota_read() 消费，因此上层写
 *          W25Q64 期间也不会因 USART2 无 FIFO 而丢字节。典型调用顺序：
 *            esp_at_ota_reset_rx() -> esp_at_ota_connect() -> esp_at_ota_start_download()
 *            -> 循环 esp_at_ota_read() 取数据 -> esp_at_ota_exit() -> esp_at_ota_close()
 * @note    注意事项：
 *            - 透传期间串口被独占，禁止调用任何 AT 指令接口；
 *            - 退出透传需发送 "+++"，前后各留 1 秒静默；
 *            - 下载总长度取自 HTTP 响应的 Content-Length，由上层负责解析与校验。
 */
#include "esp_at_ota.h"
#include "usart.h"              /* huart2 */
#include "esp_at.h"             /* esp_at_send_command 等 AT 接口 */
#include "cmsis_os.h"           /* osDelay 等 RTOS 延时 */

/* 分级日志框架：本模块 TAG=OTA */
#define LOG_TAG "OTA"
#include "log.h"

/* ================= 调试开关 ================= */
#define ESP_AT_OTA_DEBUG   0    /* 1=打印下载调试日志，0=关闭 */

/* ================= 透传控制常量 ================= */
#define OTA_PASSTHROUGH_GUARD_MS     1000u   /* +++ 前后静默时间 */

/* ================= 透传接收环形缓冲 ================= */
/* 缓冲容量：串口收数速度远低于上层消费速度，4KB 足以覆盖一次写 Flash 期间到达的字节 */
#define OTA_RX_BUF_SIZE    4096u

/* 以下变量在中断与任务中同时访问，使用 volatile 防止编译器优化 */
static volatile uint8_t  ota_rx_buf[OTA_RX_BUF_SIZE];
static volatile uint32_t ota_rx_head = 0;   /* ISR 写指针（下一个写入位置） */
static volatile uint32_t ota_rx_tail = 0;   /* 任务读指针（下一个读取位置） */

/* ================= 内部状态 ================= */
bool g_in_passthrough = false;   /* 是否处于透传模式 */
extern uint8_t rx_data;          /* esp_at.c 的单字节中断接收缓冲 */

/**
 * @brief  中断回调专用：向透传环形缓冲写入 1 字节。
 * @note   仅 USART2 接收中断调用；满则丢弃（正常情况下不会满）。
 */
void esp_at_ota_push_byte(uint8_t b)
{
    /* 环形缓冲判满：(head + 1) % SIZE == tail */
    uint32_t next = (ota_rx_head + 1u) % OTA_RX_BUF_SIZE;
    if (next != ota_rx_tail)          /* 缓冲未满，写入并前移写指针 */
    {
        ota_rx_buf[ota_rx_head] = b;
        ota_rx_head = next;
    }
    /* 满则不处理 丢弃该字节 */
}

/**
 * @brief  进透传前复位环形缓冲（头尾清零）。
 *         环形buffer head=tail 表示空 都等于0就是初始化
 */
void esp_at_ota_reset_rx(void)
{
    ota_rx_head = 0;
    ota_rx_tail = 0;
}

/**
 * @brief  通过ESP AT建立 TCP 连接（发送指令AT+CIPSTART="TCP","[服务器主机IP]","[服务器端口]"）。
 *         建立tcp连接的目的是在 ESP 模块和固件服务器之间拉起一条可靠的字节传输管道 确保字节不丢失不乱序
 */
bool esp_at_ota_connect(const char *host, uint16_t port)
{
    char cmd[128];

    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n", host, port);

    /* 使用 internal 接口：OTA 下载期间自身的 AT 命令不受 g_ota_busy 拦截 */
    return esp_at_send_command_internal(cmd, 10000u);
}

/**
 * @brief  进入透传模式并发送HTTP GET报文。
 *         进入透传模式让 TCP 原始字节直通 USART2，一块块流式落盘。
 *         发送HTTP GET请求告诉服务器当前需要的固件是哪一个 从什么位置下载 服务器将对应的固件文件作为响应体返回
 */
bool esp_at_ota_start_download(const char *host, const char *url, uint32_t content_len)
{
    char cmd[256];
    char get_req[512];
    int  get_len;

    (void)content_len;

    /* 组装 HTTP GET 请求报文 */
    get_len = snprintf(get_req, sizeof(get_req),
                       "GET %s HTTP/1.1\r\n"
                       "Host: %s\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       url, host);

    /* 切换到透传模式 */
    snprintf(cmd, sizeof(cmd), "AT+CIPMODE=1\r\n");
    esp_at_send_command_internal(cmd, 2000u);
    HAL_UART_Transmit(&huart2, (const uint8_t *)"AT+CIPSEND\r\n", 12u, 1000u);
    osDelay(300);/* 固定延时等待 ESP 回送 '>' 提示符（未严格判等，简化处理） */

    /* 保持 USART2 单字节中断接收运行：透传期间收到的字节由 esp_at.c 的串口中断逐字节喂给
     * esp_at_ota_push_byte() 进入环形缓冲。先复位环形缓冲再置标志，避免丢掉首个字节。 */
    esp_at_ota_reset_rx();/* 复位环形 buffer */
    g_in_passthrough = true;/* 标记进入透传模式 */

    /* 发送 HTTP GET 报文（透传数据，不走 AT 接口） */
    HAL_UART_Transmit(&huart2, (const uint8_t *)get_req, (uint16_t)get_len, 5000u);

#if ESP_AT_OTA_DEBUG
     LOG_I("passthrough started, GET len=%d\r\n", get_len);
#endif

    return true;
}

/**
 * @brief  从透传流中读取一帧数据（从环形缓冲读，带超时）。
 * @return >0=实际读取字节数；0=超时；<0=出错（非透传状态）。
 */
int esp_at_ota_read(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms)
{
    uint32_t tick_start = HAL_GetTick();
    uint32_t got = 0;/* 已获取字节数 */

    if (!g_in_passthrough)
    {
        return -1;/* 非透传状态 */
    }

    /* 从环形缓冲取数据，最多 max_len 字节；中断在后台持续填充，写 Flash 期间不会因串口溢出丢字节 */
    while (got < max_len)
    {
        if (ota_rx_head != ota_rx_tail)      /* 缓冲中有数据 */
        {
            buf[got++] = ota_rx_buf[ota_rx_tail];
            ota_rx_tail = (ota_rx_tail + 1u) % OTA_RX_BUF_SIZE;
        }
        else/* 缓冲区数据已读完 */
        {
            if ((HAL_GetTick() - tick_start) >= timeout_ms)
            {
                return (got > 0) ? (int)got : 0;   /* 部分或超时 */
            }
        }
    }

    return (int)got;
}

/**
 * @brief  退出透传模式（发送 +++）。
 */
bool esp_at_ota_exit(void)
{
    const char *exit_seq = "+++";

    if (!g_in_passthrough)/* 本来就不在透传模式，无需退出 */
    {
        return true;
    }

    /* 退出透传前后各留 1 秒静默 */
    osDelay(pdMS_TO_TICKS(OTA_PASSTHROUGH_GUARD_MS));
    HAL_UART_Transmit(&huart2, (const uint8_t *)exit_seq, 3u, 1000u);/* 经串口直接发送退出序列，不再转交其他接口 */
    osDelay(pdMS_TO_TICKS(OTA_PASSTHROUGH_GUARD_MS));

    g_in_passthrough = false;

    /* 确保中断接收使能（正常应仍处于使能状态，此处兜底） */
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    __HAL_UART_CLEAR_IDLEFLAG(&huart2);
    HAL_UART_Receive_IT(&huart2, &rx_data, 1);

#if ESP_AT_OTA_DEBUG
    LOG_I("passthrough exited\r\n");
#endif

    return true;
}

/**
 * @brief  关闭 TCP 连接（AT+CIPCLOSE）。
 */
void esp_at_ota_close(void)
{
    if (g_in_passthrough)
    {
        esp_at_ota_exit();
    }
    /* 使用 internal 接口：不受 g_ota_busy 拦截 */
    esp_at_send_command_internal("AT+CIPCLOSE\r\n", 3000u);
}
