/**
 * @file    esp_at.c
 * @brief   ESP-AT 模组驱动：AT 指令收发、Wi-Fi / HTTP / SNTP
 * @details 发送走 USART2 TX 的 DMA，接收走 USART2 单字节中断；中断内按行分派：
 *            - 命中预设应答（OK / ERROR / busy / ready）-> 释放信号量唤醒等待任务；
 *            - 命中 +MQTTxxx 事件行 -> 转交 esp_at_mqtt 处理；
 *            - 透传模式下 -> 逐字节喂给 esp_at_ota 环形缓冲。
 *          指令发送分两个入口：
 *            - esp_at_send_command()          供普通业务使用（透传/OTA 期间拒绝发送）
 *            - esp_at_send_command_internal() 供 OTA 自身使用（不检查占用标志）
 * @note    注意事项：
 *            - 请先调用 esp_at_Init() 完成模组初始化，再调用其余接口；
 *            - 同一时刻只允许一个任务在等待响应；
 *            - 收发均为异步实现，调试打印由 ESP_AT_DEBUG 控制，默认关闭。
 */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "usart.h"
#include "esp_at.h"
#include "esp_at_ota.h"
#include "esp_at_mqtt.h"

/* 分级日志框架：本模块 TAG=ESP */
#define LOG_TAG "ESP"
#include "log.h"

extern bool g_in_passthrough;  /* 定义在 esp_at_ota.c：OTA 透传下载进行中为 true */
bool g_ota_busy = false;       /* OTA 下载流程进行中为 true（普通 AT 指令据此让路） */

#define ESP_AT_DEBUG 0/* 调试开关：1=启用调试打印，0=关闭（减少资源占用） */

/* 计算数组元素个数（宏函数） */
#define SIZE(arr) sizeof((arr))/sizeof(((arr)[0]))

static void esp_at_usart_write(const char *data);       /* 发送 AT 指令 */
static bool esp_at_wait_ready(uint64_t timeout_ms);     /* 等待 ESP 模块返回 "ready" */
static bool esp_at_wait_boot(uint64_t timeout_ms);      /* 探测模块是否已就绪 */

/* 串口 TX DMA 句柄 */
extern DMA_HandleTypeDef hdma_usart2_tx;

/* AT 应答类型枚举 */
typedef enum
{
    AT_ACK_NONE,   /* 无匹配 */
    AT_ACK_OK,     /* 收到 OK */
    AT_ACK_ERROR,  /* 收到 ERROR */
    AT_ACK_BUSY,   /* 模块忙 */
    AT_ACK_READY,  /* 模块就绪 */
}at_ack_t;
/* 应答字符串与应答类型的映射表 */
typedef struct ACK
{
    at_ack_t ack;       /* 响应类型 */
    const char* str;    /* 对应的响应字符串 */
}at_ack_match_t;
/* 预设应答字符串表 */
static const at_ack_match_t at_ack_match[]=
{
    {AT_ACK_OK , "OK\r\n"},
    {AT_ACK_ERROR , "ERROR\r\n"},
    {AT_ACK_BUSY, "busy p…\r\n"},  /* 注意：ESP实际返回可能是"busy processing"，需根据实际情况调整 */
    {AT_ACK_READY, "ready\r\n"},
};
/* 将收到的整行与预设应答表逐项比对 */
static at_ack_t esp_at_match_inter_ack(const char* arr)
{
    /* 遍历预设的响应表，查找匹配的字符串 */
    for(int i = 0; i < SIZE(at_ack_match); i++)
    {
        /* 完全匹配则返回对应应答类型 */
        if(strcmp(arr, at_ack_match[i].str) == 0)
        {
            return at_ack_match[i].ack;
        }
    }
    return AT_ACK_NONE;  /* 无匹配 */
}

/* 接收缓冲区（存放模组返回的数据，最大 1024 字节） */
uint8_t rx_data=0;/* 单字节中断接收缓冲（HAL 库另有整套接收 API） */
static char rxbuf[1024];
static uint32_t rxlen;
static char* rxline;/* 指向 rxbuf，用于判断接收到的命令行 */
static at_ack_t rxack;/* ACK 返回值容器 */
static SemaphoreHandle_t ESP_Semaphore_handle;
static volatile bool g_waiting_ack = false;/* 有任务在等待 AT ACK，供中断判断是否可复位 rxbuf */

/* 底层串口（USART2）初始化 */
static void ESP_Lowlevel_Init(void)
{
    __HAL_UART_CLEAR_OREFLAG(&huart2);/* 清除接收溢出标志 */
    __HAL_UART_CLEAR_IDLEFLAG(&huart2);/* 清除空闲标志 */
    HAL_UART_Receive_IT(&huart2,&rx_data,1);
}

/* 初始化等待：反复发 "AT" 直到模块应答 */
static bool esp_at_wait_boot(uint64_t timeout_ms)
{
	for(uint64_t t=0;t<timeout_ms;t+=100)
	{
		if(esp_at_send_command("AT\r\n",100)){return true;}
	}
	return false;
}

/* ESP上电初始化响应 */
bool esp_at_Init(void)
{
	/* 创建串口接收信号量 */
    if (ESP_Semaphore_handle) vSemaphoreDelete(ESP_Semaphore_handle);/* 重新创建信号量（避免上次初始化失败导致信号量异常） */
	ESP_Semaphore_handle=xSemaphoreCreateBinary();
	configASSERT(ESP_Semaphore_handle);
	
    ESP_Lowlevel_Init();  /* 初始化USART2通信接口 */
    g_in_passthrough = false;/* 初始处于 AT 指令模式 */

    /* 若上一次 OTA 下载被异常中断，ESP 可能仍停在透传模式 */
    vTaskDelay(pdMS_TO_TICKS(1000));                          /* 前 1 秒静默 */
    HAL_UART_Transmit(&huart2, (const uint8_t *)"+++", 3u, 1000u);
    vTaskDelay(pdMS_TO_TICKS(1000));                          /* 后 1 秒静默 */

	if(!esp_at_wait_boot(8000))
	{
		return false;
	}
	LOG_D("%s\r\n",rxbuf);
    
    /* 恢复出厂设置，确保模组处于已知状态 */
    if(!esp_at_send_command("AT+RESTORE\r\n", 5000)){
        return false;  /* 恢复出厂设置失败 */
    }
	LOG_D("%s\r\n",rxbuf);
    
	/* AT+RESTORE 会触发模组重启，需等待约 2s 才能再次通信 */
    vTaskDelay(pdMS_TO_TICKS(2000));
	
    /* 等待模块重启完成并报出 "ready" */
    if(!esp_at_wait_ready(8000)){
        return false;  /* 模块未就绪 */
    }
	LOG_D("%s\r\n",rxbuf);
    return true;  /* 初始化成功 */
}

/* 经 DMA 异步发送 AT 指令，调用方需自行在末尾带上 \r\n */
static void esp_at_usart_write(const char *data)
{
	uint32_t len=strlen(data);
	
    __HAL_DMA_CLEAR_FLAG(&hdma_usart2_tx,DMA_FLAG_TCIF2_6);
    HAL_UART_Transmit_DMA(&huart2,(const uint8_t *)data,len);
}

/* 下发指令并等待预设应答（OK / ERROR / busy / ready） */
static at_ack_t esp_at_wait_receive(uint64_t timeout_ms)
{
	/* 复位接收缓冲，丢弃上一轮残留，使本次匹配从缓冲区头部开始 */
	g_waiting_ack = true;/* 先置标志再复位，防止中断在此窗口复位 rxbuf 丢掉正在接收的普通 AT 响应 */
	rxlen=0;
	rxline=rxbuf;
	/* 阻塞等待中断释放信号量；超时未等到则返回 AT_ACK_NONE */
	bool succeed_acked=xSemaphoreTake(ESP_Semaphore_handle,pdMS_TO_TICKS(timeout_ms))==pdPASS;
	g_waiting_ack = false;/* 等待结束，当前不再等待普通 AT 响应的 ACK */
	return succeed_acked ? rxack : AT_ACK_NONE;
}

/* 等待模块返回 "ready" */
static bool esp_at_wait_ready(uint64_t timeout_ms)
{
    return (esp_at_wait_receive(timeout_ms) == AT_ACK_READY);
}

/* 底层发送入口：不检查透传/OTA 标志，供 OTA 模块自身使用 */
bool esp_at_send_command_internal(const char* command, uint64_t timeout_ms)
{
#if ESP_AT_DEBUG
     LOG_D("Send: %s\n", command);
#endif

    esp_at_usart_write(command);
    at_ack_t ack = esp_at_wait_receive(timeout_ms);

#if ESP_AT_DEBUG
    LOG_D("Response:\n%s\n", rxbuf);
#endif

    return ack == AT_ACK_OK;
}

/* 发送AT指令并等待"OK"响应（对上层统一入口） */
bool esp_at_send_command(const char* command, uint64_t timeout_ms)
{
    /* OTA 透传或下载流程期间，ESP 正被 OTA 独占使用，此时若再发 AT 指令，
     * 指令会被混入数据流中（污染固件字节流）。因此直接拒绝发送。 */
    if (g_in_passthrough || g_ota_busy)
    {
        return false;
    }
	/* 非透传且 OTA 未占用时，才真正下发指令 */
    return esp_at_send_command_internal(command, timeout_ms);
}
/** @brief 取回最近一次接收缓冲（AT 响应行） */
const char* esp_at_get_response(void)
{
	return rxbuf;
}

/* 串口接收中断：非透传下监听 ESP 回应并按类型分发（ACK 解析 / MQTT 事件缓存）；
   透传模式下把收到的字节逐个喂给 esp_at_ota 环形缓冲。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance==USART2)
    {
        if (g_in_passthrough)/* 透传模式：字节流逐个喂给缓冲区 */
		{
			esp_at_ota_push_byte(rx_data);
		}
		else if(rxlen < sizeof(rxbuf) - 1)/* 非透传就检测AT响应 */					
		{							
			rxbuf[rxlen++] = rx_data;/* 存入接收缓冲 */
			/* 检测到换行符\n，说明一行数据接收完成（ESP响应以\r\n结尾） */
			if(rxbuf[rxlen - 1] == '\n')
			{
				rxbuf[rxlen] = '\0';/* 行末补字符串结束符 */
				at_ack_t ack_received = esp_at_match_inter_ack(rxline);/* 匹配当前行是否为预设响应 */
				if(ack_received != AT_ACK_NONE)/* 命中预设应答，进入应答处理分支 */
				{
					/* 记录应答类型并释放信号量，唤醒等待中的任务 */
					rxack=ack_received;
					BaseType_t pxHigherPriorityTaskWoken=pdFALSE;
					xSemaphoreGiveFromISR(ESP_Semaphore_handle,&pxHigherPriorityTaskWoken);
					portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
					rxline =rxbuf + rxlen;/* ACK 行移动至下一行 */
				}
				/* 非应答行：可能是 MQTT 事件，转交 esp_at_mqtt 处理 */
				else if (strncmp(rxline, "+MQTTSUBRECV:", 13) == 0)
				{
					/* MQTT 订阅消息事件：转交 esp_at_mqtt */
					esp_at_mqtt_on_subrecv(rxline);
					if (!g_waiting_ack) { rxlen = 0; rxline = rxbuf; }/* 空闲期即时消费并复位，防 rxbuf 累积满 */
					else { rxline = rxbuf + rxlen; }/* 此时要等待ack 继续下一行匹配 */
				}
				else if (strncmp(rxline, "+MQTTCONNECTED", 14) == 0)
				{
					/* 当 MQTT 连接建立时，会提示 +MQTTCONNECTED:... 。 */
					esp_at_mqtt_on_conn_evt(true);/* 通知 MQTT 层：已连接 */
					if (!g_waiting_ack) { rxlen = 0; rxline = rxbuf; }
					else { rxline = rxbuf + rxlen; }/* 此时要等待ack 继续下一行匹配 */
				}
				else if (strncmp(rxline, "+MQTTDISCONNECTED", 17) == 0)
				{
					/* 当 MQTT 连接断开时，会提示 +MQTTDISCONNECTED: 。 */
					esp_at_mqtt_on_conn_evt(false);/* 通知 MQTT 层：已断开 */
					if (!g_waiting_ack) { rxlen = 0; rxline = rxbuf; }
					else { rxline = rxbuf + rxlen; }/* 此时要等待ack 继续下一行匹配 */
				}
				/* 非MQTT消息 也没接收到ACK响应 */
				else
				{
					rxline =rxbuf + rxlen;/* 其他行（命令响应数据）移动至下一行 */
				}
			}
            
		}
        /* 继续接收下一个字符 */
        __HAL_UART_CLEAR_OREFLAG(&huart2);/* 清除接收溢出标志 */
        __HAL_UART_CLEAR_IDLEFLAG(&huart2);/* 清除空闲标志 */
        HAL_UART_Receive_IT(huart, &rx_data, 1);
    }
}
/* 出错回调：发生溢出等错误时不会进入接收完成回调，而是进入这里 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        /* 透传期间串口中断会持续往环形缓冲喂字节；出现 ORE 溢出错误时，
         * 只需清除标志并重新使能接收，保证接收不被打断。 */
        if (g_in_passthrough)
        {
            __HAL_UART_CLEAR_OREFLAG(&huart2);
            __HAL_UART_CLEAR_IDLEFLAG(&huart2);
            HAL_UART_Receive_IT(huart, &rx_data, 1);
            return;
        }
        /* 非透传模式出错：HAL 已复位状态机并关闭接收，此处重新使能接收 */
        HAL_UART_Receive_IT(huart, &rx_data, 1);
    }
}

/*******************************************************  WIFI  *******************************************************/

/* 设置 Wi-Fi 工作模式为 Station */
bool esp_at_wifi_init(void)
{
    return esp_at_send_command("AT+CWMODE=1\r\n", 2000);
}
/* 连接路由器（供后续 HTTP / MQTT 使用） */
bool esp_at_connect_wifi(char* ssid ,char *pwd ,char* bssid)
{
	/* 先确保处于 Station 模式 */
	if(!esp_at_send_command("AT+CWMODE=1\r\n",2000)){return false;}
	
	/* 拼接 AT+CWJAP 指令：ssid、pwd 必填，bssid 可选 */
	if(ssid==NULL){return false;}
	char cmd[256];
	uint16_t len1=snprintf(cmd,SIZE(cmd),"AT+CWJAP=\"%s\",\"%s\"",ssid,pwd);/* snprintf 返回已写入长度，便于在其后追加内容 */
	uint16_t len2=0;
    if(bssid)/* 提供了 bssid 时追加第三个参数 */
	{
		len2=snprintf(cmd+len1,SIZE(cmd)-len1,",\"%s\"",bssid);
	}

    snprintf(cmd+len1+len2,SIZE(cmd)-len1-len2,"\r\n");/* 补\r\n */
	
    return esp_at_send_command(cmd,5000);
}

/***** 解析wifi信息（提取响应字符串中的有用部分并传入自定义的信息结构体接收信息） *****/

/* 解析 +CWSTATE 连接状态 */
static bool cwstate_parse(wifi_info_t* info, const char* response)
{
	/* 定位到 "+CWSTATE:" 之后再解析 */
	response = strstr(response, "+CWSTATE:");
	if (response == NULL) { return false; }
	int wifi_state = 0;
	int ret = sscanf(response, "+CWSTATE:%d,\"%63[^\"]\"",&wifi_state,info->ssid);
	if (ret != 2) { return false; }
	info->connected = (wifi_state == 2);/* 手册定义：状态 2 表示已连接 */
	return true;
}
/* 解析 +CWJAP 详细信息 */
static bool cwjap_parse(wifi_info_t* info, const char* response)
{
	response = strstr(response, "+CWJAP:");
	if (response == NULL) { return false; }
	int ret = sscanf(response, "+CWJAP:\"%63[^\"]\",\"%17[^\"]\",%d,%d", 
		info->ssid,info->bssid,&info->channel, &info->rssi);
	if (ret != 4) { return false;}
	return true;
}
/* 获取 Wi-Fi 连接状态与详细信息 */
bool esp_at_get_wifi_info(wifi_info_t *info)
{
	/* AT+CWSTATE?查询wifi连接状态 */
	if(!esp_at_send_command("AT+CWSTATE?\r\n",5000)){return false;}
	if(!cwstate_parse(info,esp_at_get_response())){return false;}
	
	/* AT+CWJAP?获取具体信息 */
	if(info->connected)/* 仅已连接时才解析详细信息；未连接时连接状态本身即为有效结果，直接返回成功 */
	{
		if(!esp_at_send_command("AT+CWJAP?\r\n",5000)){return false;}
		if(!cwjap_parse(info,esp_at_get_response())){return false;}
	}
	return true;
}


/*******************************************************  WEATHER  *******************************************************/

/* 解析心知天气响应报文 */
bool seniverse_parse(const char* response, weather_info_t* info)
{
	int ret = 0;
	/* 逐级定位：先找到 results 段 */
	const char* results = strstr(response, "\"results\":");
	if (results == NULL) { return false; }
	
	const char* location = strstr(results, "\"location\":");
	if (location == NULL) { return false; }

	/* 定位到 name 字段并解析城市名称 */
	const char* name = strstr(location, "\"name\":");
	if (name) 
	{ 
		ret = sscanf(name, "\"name\":\"%31[^\"]\"", info->City);
		if (ret != 1) { return false; }
	}
	/* 定位到 path 字段并解析城市路径 */
	const char* path = strstr(location, "\"path\"");
	if (path)
	{
		ret = sscanf(path, "\"path\":\"%128[^\"]\"", info->Path);
		if (ret != 1) { return false; }
	}
	/* 回到 results 段内定位 now 段 */
	const char* now = strstr(results, "\"now\":");
	if (now == NULL) { return false; }
	/* 定位到 text 字段并解析天气描述 */
	const char* text = strstr(now, "\"text\":");
	if (text)
	{
		ret = sscanf(text, "\"text\":\"%15[^\"]\"", info->Weather);
		if (ret != 1) { return false; }
	}
	/* 定位到 code 字段并解析天气代码 */

	const char* code = strstr(now, "\"code\":");
	if (code)
	{
		char code_str[8]; /* 临时存储code的字符串形式 */
		ret = sscanf(code, "\"code\":\"%7[^\"]\"", code_str);
		if (ret != 1) { return false; }
		info->Code = atoi(code_str); /* 字符串转整数 */
	}
	/* 定位到 temperature 字段并解析温度 */
	
	char t_temperature[16]={0};
	const char* temperature = strstr(now, "\"temperature\":");
	if (temperature)
	{
		ret = sscanf(temperature, "\"temperature\":\"%15[^\"]\"", t_temperature);
		if (ret == 1) {
            float temp_value = atof(t_temperature);/* 将解析到的字符串转化为浮点数 */
			info->Temperature = temp_value;

		}
	}
	return true;
}

/* 以 HTTP GET 访问指定 URL，返回响应体；失败返回 NULL */
const char* esp_at_get_http(const char*url)
{
	static char txbuf[1024];
	snprintf(txbuf,SIZE(txbuf),"AT+HTTPCLIENT=2,1,\"%s\",,,2\r\n",url);
	bool ret=esp_at_send_command(txbuf,15000);
	const char* response=esp_at_get_response();
	return ret?response:NULL;
}

/*******************************************************  SNTP  *******************************************************/

/* 解析 +CIPSNTPTIME 时间信息 */
/* 月份英文缩写转数字（1~12） */
static uint8_t month_str_match_num(const char*month_str)
{
	const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	for (uint8_t i = 0; i < SIZE(months); i++)
	{
		if (strcmp(month_str, months[i]) == 0)
		{
			return i + 1;
		}
	}
	return 0;
}
/* 星期英文缩写转数字（1~7） */
static uint8_t weekday_str_match_num(const char* weekday_str)
{
	const char* weekdays[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
	for (uint8_t i = 0; i < SIZE(weekdays); i++) {
		if (strcmp(weekday_str, weekdays[i]) == 0)
		{
			return i + 1;
		}
	}
	return 0;
}
/** @brief 解析 "+CIPSNTPTIME:" 响应为日期时间结构 */
static bool cipsntptime_parse(const char* response, date_info_t* info)
{
	int ret = 0;
	char month_str[8], weekday_str[8],day_str[4],hour_str[4], minute_str[4], second_str[4], year_str[8];
	response = strstr(response, "+CIPSNTPTIME:");
	if(response)
	{ 
		ret = sscanf(response, "+CIPSNTPTIME:%3s %3s %2s %2s:%2s:%2s %4s",
			weekday_str,month_str, day_str, hour_str, minute_str, second_str, year_str );
		if (ret != 7) { 
			LOG_W("sntp parse fail\r\n");
			return false; 
		}
		/* 字符串转整数 */
		info->day=atoi(day_str);
		info->hour = atoi(hour_str);
		info->minute = atoi(minute_str);
		info->second = atoi(second_str);
		info->year = atoi(year_str);
		info->month = month_str_match_num(month_str);
		info->weekday = weekday_str_match_num(weekday_str);
		return true;
	}
	return false;
}

/**
 * @brief  配置 SNTP 服务（开启并设置东八区 UTC+8）
 * @return true=配置成功；false=AT 指令执行失败
 */
bool esp_at_sntp_init(void)
{
	if(!esp_at_send_command("AT+CIPSNTPCFG=1,8\r\n",5000)){return false;}
	return true;
}
/**
 * @brief  获取 SNTP 时间并解析为日期时间结构
 * @param  info 输出：解析结果
 * @return true=获取并解析成功；false=AT 指令失败或响应解析失败
 */
bool esp_at_get_time(date_info_t* info)
{
	if(!esp_at_send_command("AT+CIPSNTPTIME?\r\n",5000)){return false;}
	if(!cipsntptime_parse(esp_at_get_response(),info)){return false;}
	return true;
}

