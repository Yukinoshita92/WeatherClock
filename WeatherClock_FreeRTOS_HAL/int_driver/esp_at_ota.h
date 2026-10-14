/**
 * @file    esp_at_ota.h
 * @brief   ESP 透传下载接口（OTA 专用）
 * @details 透传模式下 ESP 不再解析 AT 指令，TCP 收到的字节原样直通 USART2，上层持续
 *          读取并写入 W25Q64，内存占用小、吞吐高，适合数百 KB 的固件；数据经独立的
 *          环形缓冲承接，不与 AT 应答缓冲混用。
 * @note    注意事项：
 *            - 透传是独占的，期间不得调用任何 AT 指令接口；
 *            - 退出透传需发送 "+++"，前后各留 1 秒静默，收到 OK 后恢复 AT 模式；
 *            - 下载总长度需由 Content-Length 提前获知，用于判断何时收满。
 */
#ifndef __ESP_AT_OTA_H__
#define __ESP_AT_OTA_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief  通过 ESP 建立 TCP 连接（AT+CIPSTART）。
 * @param  host  服务器主机名/IP（字符串）。
 * @param  port  服务器端口号。
 * @return true=连接成功；false=失败。
 */
bool esp_at_ota_connect(const char *host, uint16_t port);

/**
 * @brief  发送一条 HTTP GET 请求并进入透传模式（AT+CIPSEND 透传）。
 * @param  url      请求的完整 URL 路径（含 /xx.bin）。
 * @param  host     服务器主机名（用于 Host 头）。
 * @param  content_len 期望接收的固件总字节数（用于设置超时/收满判断）。
 * @return true=已进入透传，等待读数据；false=失败。
 */
bool esp_at_ota_start_download(const char *host, const char *url, uint32_t content_len);

/**
 * @brief  从透传流中读取一帧数据。
 * @param  buf        接收缓冲区。
 * @param  max_len    本次最多读取的字节数。
 * @param  timeout_ms 超时（毫秒），0 表示无限等待。
 * @return >0 = 实际读取到的字节数；0 = 超时；<0 = 出错。
 */
int esp_at_ota_read(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms);

/**
 * @brief  退出透传模式（发送 +++ 并等待 OK）。
 * @return true=成功退出；false=失败（可能已断链）。
 */
bool esp_at_ota_exit(void);

/**
 * @brief  关闭 TCP 连接（AT+CIPCLOSE）。
 * @note   退出透传后调用，释放连接。
 */
void esp_at_ota_close(void);

/**
 * @brief  向透传接收环形缓冲写入 1 字节（仅 USART2 接收中断调用）。
 * @note   由 esp_at.c 的 HAL_UART_RxCpltCallback 在透传期间调用，
 *         把接收到的字节送入环形缓冲，供 esp_at_ota_read() 消费。
 *         单生产者（中断）/ 单消费者（任务），无需临界区。
 */
void esp_at_ota_push_byte(uint8_t b);

/**
 * @brief  复位透传接收环形缓冲（进入透传前调用一次）。
 */
void esp_at_ota_reset_rx(void);

#endif /* __ESP_AT_OTA_H__ */
