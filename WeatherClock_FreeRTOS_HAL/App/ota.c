/**
 * @file    ota.c
 * @brief   App 端 WiFi OTA 升级任务
 * @details 周期获取服务器版本并与当前固件比较，有新版本时经 ESP 透传下载到 W25Q64
 *          暂存区，回读校验（XBIN 头部 + 负载 CRC32）通过后置"需要升级"标志并复位，
 *          由 BootLoader 完成实际的 Flash 写入：
 *            - 通知来源：OneNET MQTT 升级通知为主，HTTP 轮询 version.json 兜底；
 *            - 暂存地址：W25Q64 暂存区 SPI_STAGE_ADDR，与 BootLoader 约定一致；
 *            - 下载链接：解析通知中的 URL，http/https 统一按明文 80 端口下载。
 * @note    注意事项：
 *            - 只负责下载、暂存、校验、置标志，绝不写内部 Flash；
 *            - 下载期间独占 ESP 与串口（g_ota_busy），其他任务不得发 AT 指令；
 *            - 失败会累计计数，达上限后停止自动升级，避免坏固件反复重下。
 */

#include "ota.h"
#include "cmsis_os.h"           /* osThreadNew / osDelay 等 */
#include "magic_header.h"       /* XBIN 头部解析 */
#include "Int_W25Q64.h"         /* W25Q64 驱动 */
#include "esp_at_ota.h"         /* 透传下载 */
#include "ota_flag.h"           /* BKUP 标志封装 */
#include "esp_at.h"             /* 需要 g_in_passthrough 等 */
#include "onenet_mqtt.h"        /* OneNET MQTT 接入层（9.29 新增） */
/* 版本号比较 + EEPROM 配置区常量 */
#include "ota_version.h"
#include "eeprom_flag_header.h"
#include "app_config.h"      /* 配置收口：固件托管地址等统一提供 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 分级日志框架：本模块 TAG=OTA */
#define LOG_TAG "OTA"
#include "log.h"                     /* 必须先 #define LOG_TAG，再包含本头文件 */

#define DEBUG_OTA 1

/* ================= 配置常量 ================= */

/* 固件托管地址（腾讯云 COS，请按实际部署修改）—— 域名由 app_config.h 统一提供 */
#define OTA_FIRMWARE_HOST       CFG_OTA_HOST
#define OTA_FIRMWARE_PORT       CFG_OTA_PORT
#define OTA_FIRMWARE_PATH       CFG_OTA_PATH
#define APP_HEADER_ADDR         0x08008000u          /* 运行固件 XBIN 头所在地址 */
#define OTA_VERSION_PATH        "/version.json"      /* 新固件版本信息存放路径 */

/* ================= 第四阶段 MQTT 开关（9.29 新增） ================= */
#define OTA_MQTT_ENABLE        1   /* 1=MQTT 通知主路径（OneNET）；0=关闭 */
#define OTA_HTTP_FALLBACK      1   /* 1=保留 HTTP 轮询作降级兜底；0=关闭 */

/* 轮询间隔：30 秒 */
#define OTA_POLL_INTERVAL_MS    (30UL * 1000UL)

/* OTA 任务栈大小与优先级 */
#define OTA_TASK_STACK          (2048U)             /* 透传+写Flash 需要较大栈 */
#define OTA_TASK_PRIORITY       (osPriorityNormal5) /* 高于 workqueue(BelowNormal5)，下载期间不被抢占，减少 ORE 丢字节 */

/* ================= W25Q64 暂存区地址（与 BootLoader 一致） ================= */

#define SPI_STAGE_ADDR          0x001000u           /* OTA 新固件暂存区 */
/* 0x200000u 为 App 备份区（预留）；暂存区大小 = 0x200000 - 0x001000 = 0x1FF000 = 2044KB */
#define SPI_STAGE_SIZE          0x1FF000u           /* OTA 新固件暂存区大小 */
#define XBIN_HEADER_SIZE        4096u               /* XBIN 头部固定 4096 字节 */

/* ================= 分块参数 ================= */
#define OTA_CHUNK_SIZE          1024u               /* 单次读写分块 1KB */

/* HTTP 响应头解析缓冲上限 */
#define OTA_HTTP_HDR_MAX        1024u

extern bool g_ota_busy;

/* ================= 内部缓冲区 ================= */
/* 分块读缓冲（1KB），用静态变量避免栈占用 */
static uint8_t  ota_chunk_buf[OTA_CHUNK_SIZE];
/* 头部回读缓冲（此处只需 sizeof(magic_header_t)=256 字节，用 4KB 便于扩展） */
static uint8_t  ota_header_buf[XBIN_HEADER_SIZE];

/* ================= 软件 CRC32 累加器（与 crc32.c 的 zlib 算法一致） ================= */
/* CRC32 查找表（与 crc32.c 相同，反转多项式 0xEDB88320） */
static uint32_t ota_crc_table[256];
static int      ota_crc_table_ready = 0;
/* 生成 CRC32 查找表（首次调用时执行） */
static void ota_crc_make_table(void)
{
    uint32_t i;
    int j;
    for (i = 0; i < 256; i++)
    {
        uint32_t c = i;
        for (j = 0; j < 8; j++)
        {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        ota_crc_table[i] = c;
    }
    ota_crc_table_ready = 1;
}
/* 流式 CRC 上下文，支持分块累加 */
typedef struct
{
    uint32_t crc;               /* 当前 CRC 中间值（未最终异或） */
} ota_crc_ctx_t;
/* 初始化流式 CRC，初值 0xFFFFFFFF（与 zlib 一致） */
static void ota_crc_init(ota_crc_ctx_t *ctx)
{
    if (!ota_crc_table_ready)
    {
        ota_crc_make_table();
    }
    ctx->crc = 0xFFFFFFFFu;
}
/* 累加一段数据 */
static void ota_crc_update(ota_crc_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
    {
        ctx->crc = ota_crc_table[(ctx->crc ^ data[i]) & 0xFFu] ^ (ctx->crc >> 8);
    }
}
/* 结束累加，返回最终 CRC（最终异或 0xFFFFFFFF） */
static uint32_t ota_crc_final(ota_crc_ctx_t *ctx)
{
    return ctx->crc ^ 0xFFFFFFFFu;
}
/**
 * @brief  计算 W25Q64 中某段数据的 CRC32（分块回读累加）。
 * @param  addr  W25Q64 起始地址。
 * @param  len   数据长度。
 * @return CRC32 值。
 */
static uint32_t ota_flash_crc(uint32_t addr, uint32_t len)
{
    ota_crc_ctx_t ctx;
    uint32_t remain = len;
    uint32_t off = 0;

    ota_crc_init(&ctx);

    while (remain > 0)
    {
        uint32_t chunk = remain > OTA_CHUNK_SIZE ? OTA_CHUNK_SIZE : remain;
        INT_W25Q64_Read_with_32addr(addr + off, ota_chunk_buf, (uint16_t)chunk);
        ota_crc_update(&ctx, ota_chunk_buf, chunk);
        remain -= chunk;
        off += chunk;
    }

    return ota_crc_final(&ctx);
}

/* ================= W25Q64 分块写入（页对齐由驱动层保证） ================= */
/**
 * @brief  将数据分块写入 W25Q64 指定地址。
 * @note   底层 INT_W25Q64_Write_with_32addr 已按 256 字节页边界拆分，
 *         本层按 OTA_CHUNK_SIZE 分块即可，跨页不再回绕覆盖。
 */
static int ota_w25q64_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t remain_len = len;
    uint32_t had_written_len = 0;
    while (remain_len > 0)
    {
        uint32_t write_len = (remain_len > OTA_CHUNK_SIZE) ? OTA_CHUNK_SIZE : remain_len;
        /* num 参数为 uint16_t，chunk 最大 1024，安全 */
        INT_W25Q64_Write_with_32addr(addr + had_written_len, (uint8_t *)(data + had_written_len), (uint16_t)write_len);
        had_written_len += write_len;
        remain_len -= write_len;
    }
    return 0;
}

/* ================= 头部校验 ================= */
/**
 * @brief  校验暂存区中的 XBIN 头部（magic + 头部自身 CRC + 机型 + 类型）。
 * @return true=头部有效，false=无效。
 */
static bool ota_header_check(magic_header_t *hdr)
{
    /* 回读头部（前 sizeof(magic_header_t)=256 字节已含全部字段） */
    INT_W25Q64_Read_with_32addr(SPI_STAGE_ADDR, ota_header_buf, sizeof(magic_header_t));
    memcpy(hdr, ota_header_buf, sizeof(magic_header_t));   /* 拷贝到调用者传入的结构体 */

    /* DEBUG：打印 magic 与前 16 字节，便于定位头部失败 */
    #if (DEBUG_OTA==1)
    {
        int _i;
        printf("[OTA] hdr magic=0x%08lX :", (unsigned long)hdr->magic);
        for (_i = 0; _i < 16; _i++) { printf(" %02X", ota_header_buf[_i]); }
        printf("\r\n");
    }
    #endif

    /* 1. 校验魔数 + 头部自身 CRC */
    if (!magic_header_validate(hdr))
    {
        LOG_E("header validate fail\r\n");
        return false;
    }

    /* 2. 机型校验（防错刷） */
    if (magic_header_get_hw_model(hdr) != MAGIC_HEADER_HW_MODEL_WEATHERCLOCK_F407)
    {
        LOG_E("hw model mismatch\r\n");
        return false;
    }

    /* 3. 类型必须是固件（非资源包） */
    if (magic_header_get_type(hdr) != MAGIC_HEADER_TYPE_APP)
    {
        LOG_E("not app firmware type\r\n");
        return false;
    }

    return true;
}

/* ================= HTTP 响应头解析（取 Content-Length） ================= */
/**
 * @brief  读取 HTTP 响应头（直到 "\r\n\r\n"），解析出 Content-Length。
 * @param  content_len  输出：响应体字节数（固件长度）。
 * @return 0=成功；-1=超时/头过长；-2=未找到 Content-Length。
 */
static int ota_parse_http_header(uint32_t *content_len)
{
    static uint8_t hdr[OTA_HTTP_HDR_MAX];   /* HTTP 响应头接收缓冲（静态，不占任务栈） */
    uint32_t len = 0;
    int had_found_http_header = 0;

    *content_len = 0;                       /* 预置 0，避免解析失败后沿用脏数据 */

    while (len < OTA_HTTP_HDR_MAX - 1)      /* 未超出响应头缓冲上限 */
    {
        uint8_t ch;
        if (esp_at_ota_read(&ch, 1u, 3000u) <= 0)   /* 逐字节从环形 buffer 读取服务器响应内容 */
        {
            return -1;   /* 服务器无响应/超时 */
        }
        hdr[len++] = ch;

        /* 匹配 "\r\n\r\n" 结束符（HTTP 头结束标志） */
        if (len >= 4 &&
            hdr[len - 4] == '\r' && hdr[len - 3] == '\n' &&
            hdr[len - 2] == '\r' && hdr[len - 1] == '\n')
        {
            hdr[len] = '\0';                /* 补结束符，供后续 strstr 解析 */
            had_found_http_header = 1;
            break;
        }
    }

    if (!had_found_http_header)
    {
        return -1;   /* 头过长或格式异常 */
    }

    /* 解析 "Content-Length: N" 获取固件实际长度 */
    {
        char *p = strstr((char *)hdr, "Content-Length:");   /* 定位字段起点 */
        if (p == NULL)
        {
            return -2;   /* 未找到 Content-Length */
        }
        *content_len = (uint32_t)strtoul(p + 15, NULL, 10);
    }

    return 0;
}

/* 提前擦除 W25Q64 指定地址空间 */
static void ota_erase_w25q64(uint32_t addr, uint32_t len)
{
    uint32_t start_sector = addr / 4096;
    uint32_t end_sector   = (addr + len - 1) / 4096;

    for (uint32_t s = start_sector; s <= end_sector; s++)
    {
        uint8_t block  = (uint8_t)(s / 16);   /* 块号：每块 64KB */
        uint8_t sector = (uint8_t)(s % 16);   /* 块内扇区号：0~15 */

        INT_W25Q64_Erase_Sector(block, sector);
    }
}


/**
 * @brief  采用MQTT消息提取版本号内容进行判断服务器版本是否应升级：读当前版本 + 版本比较 + 失败计数门槛。
 * @param  server_ver  服务器（MQTT 通知 / version.json）提供的目标版本号。
 * @return true=应升级；false=无更新或当前固件头无效。
 */
static bool ota_should_update(const char *server_ver)
{
    magic_header_t cur_hdr;

    /* 读运行固件自身 magic 头，取当前版本 */
    memcpy(&cur_hdr, (const void *)APP_HEADER_ADDR, sizeof(cur_hdr));
    if (!magic_header_validate(&cur_hdr))
    {
        LOG_W("current header invalid, skip\r\n");
        return false;
    }

    /* 版本新旧比较（固定 3 段，防降级）：server > current 才更新 */
    if (ota_version_compare(server_ver, cur_hdr.version) <= 0)
    {
        LOG_I("no update (server=%s, current=%s)\r\n", server_ver, cur_hdr.version);
        return false;
    }

    /* 失败次数上限门槛：失败计数>=3 且服务器不高于上次尝试版本 -> 停止自动升级 */
    {
        uint32_t attempt_ver = ota_flag_get_attempt_ver();
        if (ota_flag_get_fail_count() >= EEP_OTA_FAIL_COUNT_MAX &&
            ota_version_to_u32(server_ver) <= attempt_ver)
        {
            LOG_W("OTA disabled: fail limit reached (server=%s)\r\n", server_ver);
            return false;
        }
        /* 服务器出现更高版本 -> 清零失败计数，允许重试 */
        if (ota_version_to_u32(server_ver) > attempt_ver)
        {
            ota_flag_reset_fail_count();
        }
        /* 记录本次尝试升级的目标版本 */
        ota_flag_set_attempt_ver(ota_version_to_u32(server_ver));
    }

    LOG_I("new version: %s -> %s\r\n", cur_hdr.version, server_ver);
    return true;
}

/**
 * @brief  HTTP 兜底路径：轮询 version.json 获取服务器版本（9.29 保留原逻辑并改名）。
 * @return true=有新版本；false=无更新或查询失败。
 */
static bool ota_check_update_http(void)
{
    char           url[128];
    const char    *resp;
    char           server_ver[128];
    size_t         len;

    /* 拼接版本清单 URL 并拉取 */
    snprintf(url, sizeof(url), "http://%s:%d%s",
             OTA_FIRMWARE_HOST, OTA_FIRMWARE_PORT, OTA_VERSION_PATH);
    resp = esp_at_get_http(url);
    if (resp == NULL)
    {
        LOG_W("version query fail, skip\r\n");
        return false;
    }

    /* 从 {"version":"v1.2.0"} 提取版本字符串 */
    {
        char *p, *q;
        p = strstr((char *)resp, "\"version\"");   /* 定位版本字段 */
        if (p == NULL) { LOG_W("version field not found\r\n"); return false; }
        p = strchr(p, ':');   /* 定位字段分隔的冒号 */
        if (p == NULL) { return false; }
        p = strchr(p + 1, '"');/* 版本信息在起始引号之后 */
        if (p == NULL) { return false; }
        q = strchr(p + 1, '"');/* 版本信息在结束引号之前 */
        if (q == NULL) { return false; }   /* p~q 之间即版本字符串 */
        len = (size_t)(q - p - 1);
        if (len >= sizeof(server_ver)) { len = sizeof(server_ver) - 1; }   /* 超长则截断，防止越界写 */
        memcpy(server_ver, p + 1, len);
        server_ver[len] = '\0';             /* 补字符串结束符 */

        #if DEBUG_OTA==1
        p[len+1]='\0';
        printf("[OTA] resp:%s,p:%s,ser:%s",resp,p,server_ver);
        #endif
    }

    return ota_should_update(server_ver);
}

/**
 * @brief  解析固件下载 URL（http(s)://host[:port]/path），提取 host / port / path
 * @param  url   完整 URL 字符串。
 * @param  host  输出：主机名/IP。
 * @param  port  输出：端口（https 亦归一化为 80）。
 * @param  path  输出：资源路径（含开头 '/'）。
 * @return 0=成功；-1=格式不支持。
 * @note   协议归一化：下载链路为明文 TCP（只能 http + 80），因此无论通知下发的 URL 是
 *         http:// 还是 https://，都统一按 http + 80 处理。
 */
static int ota_parse_url(const char *url, char *host, uint16_t *port, char *path)
{
    const char *p;
    const char *q;
    const char *colon;
    uint16_t    default_port;   /* 未提供具体端口时使用的默认端口 */

    if (url == NULL)
    {
        return -1;
    }

    /* http/https 统一按明文 80 处理（下载链路不支持 TLS） */
    if (strncmp(url, "http://", 7) == 0)
    {
        p = url + 7;   /* 跳过 "http://" 前缀 */
        default_port = 80;
    }
    else if (strncmp(url, "https://", 8) == 0)
    {
        p = url + 8;   /* 跳过 "https://" 前缀 */
        default_port = 80;      /* https 也归一化到明文 80 */
    }
    else
    {
        return -1;
    }

    q = strchr(p, '/');                 /* 下载路径 path 起点 */
    colon = strchr(p, ':');             /* 可能的端口分隔符 */

    if (colon != NULL && (q == NULL || colon < q))
    {
        /* 自身就带端口：host[:port] */
        size_t hlen = (size_t)(colon - p);
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        *port = (uint16_t)strtoul(colon + 1, NULL, 10);
        if (*port == 443)   /* 显式 443 也归一化到明文 80（固件下载链路仅支持明文） */
        {
            *port = 80;
        }
    }
    else
    {
        /* url中没有提供端口：用默认端口 */
        size_t hlen = (q != NULL) ? (size_t)(q - p) : strlen(p);
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        *port = default_port;
    }

    if (q != NULL)
    {
        strcpy(path, q);                /* 含开头 '/' */
    }
    else
    {
        strcpy(path, "/");
    }
    return 0;
}

/**
 * @brief  执行一次下载：透传下载 -> 写 W25Q64 -> 回读校验。
 * @param  host  固件服务器主机（从升级通知 URL 解析，9.29 改为参数化）。
 * @param  port  端口。
 * @param  path  固件路径（如 "/app_upgrade.xbin"）。
 * @return ota_result_t 结果码。
 */
static ota_result_t ota_download(const char *host, uint16_t port, const char *path)
{
    magic_header_t hdr;
    uint32_t        W25Q64_offset = 0;   /* 写入 W25Q64 的偏移量 */
    uint32_t        content_len = 0;     /* HTTP 响应头获取到的固件长度 */
    uint32_t        data_crc = 0;
    int             esp_read_len;        /* 本次从环形缓冲实际读到的字节数 */

    /* ---- 1. 擦除暂存区（发 GET 之前，此时串口空闲，避免擦除期间丢字节） ---- */
    ota_erase_w25q64(SPI_STAGE_ADDR, SPI_STAGE_SIZE);

    /* ---- 2. 建立 TCP 连接 ---- */
    if (!esp_at_ota_connect(host, port))
    {
        LOG_E("connect fail\r\n");
        return OTA_RESULT_CONNECT_FAIL;
    }

    /* ---- 3. 发 GET 并进入透传 ---- */
    if (!esp_at_ota_start_download(host, path, 0))
    {
        LOG_E("start download fail\r\n");
        esp_at_ota_close();
        return OTA_RESULT_DOWNLOAD_FAIL;
    }

    /* ---- 4. 解析 HTTP 响应头，取得 Content-Length ---- */
    {
        int hr = ota_parse_http_header(&content_len);
        if (hr != 0)                      /* 解析失败：未找到响应头或响应头超长 */
        {
            LOG_E("http header parse fail(%d)\r\n", hr);
            esp_at_ota_exit();            /* 退出透传 */
            esp_at_ota_close();
            return OTA_RESULT_DOWNLOAD_FAIL;
        }
    }

    /* ---- 5. 按 Content-Length 精确下载 body（整块读 + 写 W25Q64） ---- */
    while (W25Q64_offset < content_len)
    {
        esp_read_len = esp_at_ota_read(ota_chunk_buf, OTA_CHUNK_SIZE, 5000u);   /* 按块读取 */

        if (esp_read_len > 0)
        {
            /* 成功读到一帧，写入暂存区 */
            ota_w25q64_write(SPI_STAGE_ADDR + W25Q64_offset, ota_chunk_buf, (uint32_t)esp_read_len);
            W25Q64_offset += (uint32_t)esp_read_len;
        }
        else if (esp_read_len == 0)
        {
            /* 超时无数据：再确认一次，仍无数据则视为断链/下载结束 */
            esp_read_len = esp_at_ota_read(ota_chunk_buf, OTA_CHUNK_SIZE, 3000u);
            if (esp_read_len <= 0)
            {
                break;
            }
            ota_w25q64_write(SPI_STAGE_ADDR + W25Q64_offset, ota_chunk_buf, (uint32_t)esp_read_len);
            W25Q64_offset += (uint32_t)esp_read_len;
        }
        else
        {
            /* 未知错误，退出透传并关闭连接 */
            LOG_E("read error\r\n");
            esp_at_ota_close();
            return OTA_RESULT_DOWNLOAD_FAIL;
        }
    }

    /* ---- 6. 下载结束 退出透传、关闭连接 ---- */
    esp_at_ota_exit();
    esp_at_ota_close();

    LOG_I("downloaded %lu bytes (expect %lu)\r\n", (unsigned long)W25Q64_offset, (unsigned long)content_len);

    /* 少收字节直接判失败，避免把残缺镜像交给校验/BootLoader */
    if (W25Q64_offset != content_len)
    {
        LOG_E("download incomplete\r\n");
        return OTA_RESULT_DOWNLOAD_FAIL;
    }

    /* ---- 7. 回读校验头部 ---- */
    if (!ota_header_check(&hdr))
    {
        return OTA_RESULT_HEADER_FAIL;
    }

    /* 长度一致性：Content-Length 应等于 XBIN 头(4096) + 负载长度 */
    if (content_len != XBIN_HEADER_SIZE + magic_header_get_length(&hdr))
    {

        LOG_E("length mismatch: http=%lu, payload=%lu\r\n", (unsigned long)content_len, (unsigned long)magic_header_get_length(&hdr));
        return OTA_RESULT_HEADER_FAIL;
    }

    /* ---- 8. 回读负载计算 CRC ----
     * data_crc32 只覆盖"头部之后"的负载（data_offset=4096 起）。
     * 从 W25Q64 回读负载计算 CRC，与头部 data_crc32 比对。 */
    data_crc = ota_flash_crc(SPI_STAGE_ADDR + XBIN_HEADER_SIZE, magic_header_get_length(&hdr));
    if (data_crc != magic_header_get_crc32(&hdr))   /* 回读计算的 CRC 与头部声明不一致 */
    {

        LOG_E("payload crc mismatch: calc=0x%08lX expect=0x%08lX\r\n", (unsigned long)data_crc, (unsigned long)magic_header_get_crc32(&hdr));
        return OTA_RESULT_CRC_FAIL;
    }

    /* 固件下载成功，暂存区已就绪，打印版本号、长度等信息 */
    LOG_I("version=%s, len=%lu, crc ok\r\n", hdr.version, (unsigned long)magic_header_get_length(&hdr));

    return OTA_RESULT_OK;
}

/* ================= OTA 后台任务 ================= */

/**
 * @brief  OTA 后台任务：周期性轮询并执行下载流程。
 */
static void ota_task(void *argument)
{
    (void)argument;

    /* 首轮延时：让 workqueue 先稳定渲染主页面，再开始首次升级轮询 */
    osDelay(pdMS_TO_TICKS(OTA_POLL_INTERVAL_MS));

    LOG_I("task started\r\n");

    for (;;)
    {
        ota_result_t result = OTA_RESULT_NO_UPDATE;

#if OTA_MQTT_ENABLE
        if (onenet_mqtt_is_connected())
        {
            /* 主路径：MQTT 连接正常，等 OneNET 升级通知（事件驱动，10.03 修正） */
            onenet_ota_notify_t notify;
            if (onenet_mqtt_wait_notify(&notify, OTA_POLL_INTERVAL_MS))   /* 收到并接受 MQTT 升级通知 */
            {
                char     host[128];
                uint16_t port;
                char     path[256];
                /* URL 解析正常且版本比较通过，可以执行下载 */
                if (ota_parse_url(notify.url, host, &port, path) == 0 &&
                    ota_should_update(notify.version))
                {
                    onenet_mqtt_disconnect();           /* 下载前断开 MQTT，让出 USART2 */
                    /* bug修复：下载期间独占 ESP，禁止天气发 AT */
                    g_ota_busy = true;
                    result = ota_download(host, port, path);
                    g_ota_busy = false;
                    onenet_mqtt_reconnect();            /* 下载后重连 */
                }
            }
        }
        else
        {
            /* MQTT 断开：尝试重连 */
            onenet_mqtt_reconnect();
        }
#endif

#if OTA_HTTP_FALLBACK
        /* 降级兜底：仅在 MQTT 未连接时启用 HTTP 轮询（避免与 MQTT 事件抢占 USART2） */
        if (!onenet_mqtt_is_connected())
        {
            /* 下载期间独占 ESP，禁止天气发 AT */
            if (ota_check_update_http())
            {
                g_ota_busy = true;
                result = ota_download(OTA_FIRMWARE_HOST, OTA_FIRMWARE_PORT, OTA_FIRMWARE_PATH);
                g_ota_busy = false;
            }
            else
            {
                result = OTA_RESULT_NO_UPDATE;
            }
        }
#endif

        switch (result)
        {
            case OTA_RESULT_OK:
                LOG_I("download ok, set update flag & reset\r\n");
                /* 置"需要升级"标志，复位交给 BootLoader 接管 */
                ota_flag_init();
                ota_flag_set_need_update();
                /* 稍作延时让最后一条日志发出，然后软件复位 */
                osDelay(pdMS_TO_TICKS(100));
                NVIC_SystemReset();
                /* 不应执行到这里 */
                break;
            case OTA_RESULT_NO_UPDATE:
                LOG_I("no new firmware could update.\r\n");
                break;   /* 无更新并非失败，不累加失败计数 */
            case OTA_RESULT_CONNECT_FAIL:
            case OTA_RESULT_DOWNLOAD_FAIL:
            case OTA_RESULT_CRC_FAIL:
            case OTA_RESULT_HEADER_FAIL:
            case OTA_RESULT_FLASH_FAIL:
            default:
                LOG_W("download failed, result=%d, retry later\r\n", (int)result);
                ota_flag_inc_fail_count();   /* 累计失败次数，达到上限后停止自动升级 */
                break;
        }

        /* 等待下一次轮询 */
        osDelay(pdMS_TO_TICKS(OTA_POLL_INTERVAL_MS));
    }
}

/**
 * @brief  初始化并创建 OTA 后台任务。
 */
void ota_init(void)
{
    /* 初始化 BKUP 访问权限（置升级标志使用） */
    ota_flag_init();

    /* 创建低优先级 OTA 任务 */
    osThreadNew(ota_task, NULL,
                &(osThreadAttr_t){
                    .name       = "ota",
                    .stack_size = OTA_TASK_STACK,
                    .priority   = OTA_TASK_PRIORITY,
                });
}
