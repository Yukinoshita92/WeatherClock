/**
 * @file    app.c
 * @brief   应用主循环：以软件定时器 + 工作队列驱动各功能模块的周期刷新
 * @note    本模块日志 TAG = "APP"
 */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"                 /* FreeRTOS 软件定时器 API（xTimerCreate/xTimerStart 等） */
#include "page.h"
#include "esp_at.h"
#include "aht20.h"
#include "workqueue.h"
#include "ota_flag.h"
/* 分级日志框架：本模块 TAG=APP */
#define LOG_TAG "APP"
#include "log.h"

#include "app_config.h"          /* 配置收口 */

/* 心知天气（Seniverse）实时天气 HTTP 接口：key 与城市编号由 app_config.h 提供 */
#define WEATHER_URL  "https://api.seniverse.com/v3/weather/now.json?key=" CFG_WEATHER_KEY \
                     "&location=" CFG_WEATHER_LOC "&language=en&unit=c"

/* 时间单位换算宏：统一以毫秒为基准逐级换算，便于书写定时周期 */
#define MILLISECONDS(x) (x)
#define SECONDS(x)      MILLISECONDS((x) * 1000)
#define MINUTES(x)      SECONDS((x) * 60)
#define HOURS(x)        MINUTES((x) * 60)
#define DAYS(x)         HOURS((x) * 24)

/* 各功能模块的周期刷新间隔 */
#define TIME_SYNC_INTERVAL          HOURS(1)        /* 定时器计数上限不足 1 天，故取 1 小时 */
#define WIFI_UPDATE_INTERVAL        SECONDS(12)     /* Wi-Fi 状态刷新周期 */
#define TIME_UPDATE_INTERVAL        SECONDS(1)      /* 时钟走时刷新周期 */
#define INDOOR_UPDATE_INTERVAL      SECONDS(5)      /* 室内温湿度刷新周期 */
#define OUTDOOR_UPDATE_INTERVAL     MINUTES(1)      /* 室外天气刷新周期 */

/* 定时器句柄 */
static TimerHandle_t time_sync_timer;
static TimerHandle_t time_update_timer;
static TimerHandle_t wifi_update_timer;
static TimerHandle_t indoor_update_timer;
static TimerHandle_t outdoor_update_timer;

/*********************************************** Main Update ***********************************************/

/* Wi-Fi 状态刷新：读取 ESP-AT 上报的 AP 信息并重绘主页面 SSID */
static void wifi_update(void)
{
	/* 缓存上一次的 Wi-Fi 信息，仅当内容变化时才重绘，避免无意义刷屏 */
	static wifi_info_t last_wifi_info={0};
	
	xTimerChangePeriod(wifi_update_timer,pdMS_TO_TICKS(WIFI_UPDATE_INTERVAL),0);   /* 按新周期续期，等价于同时启动定时器 */
	
	wifi_info_t wifi_info={0};                          /* 本次读到的 Wi-Fi 信息 */
	if(!esp_at_get_wifi_info(&wifi_info))
	{	
		LOG_W("get info fail!!\r\n");
		return;
	}
	
	if(strlen(wifi_info.ssid)>=18)                      /* SSID 超出主页面预留宽度，跳过本次刷新 */
		return;
	
	if(memcmp(&wifi_info,&last_wifi_info,sizeof(wifi_info_t))==0)   /* 与上次一致，无需重绘 */
		return;
	
	if(wifi_info.connected)                             /* 仅在已连接状态下更新 */
	{
		LOG_I("SSID: %s, BSSID: %s, Channel: %d, RSSI: %d\n",
					wifi_info.ssid, wifi_info.bssid, wifi_info.channel, wifi_info.rssi);   /* 打印 Wi-Fi 信息 */
		main_page_redraw_wifi_ssid(wifi_info.ssid);     /* 重绘 SSID */
	}else
	{
		LOG_W("wifi disconnected!!");
		main_page_redraw_wifi_ssid("wifi lost");
	}
	memcpy(&last_wifi_info,&wifi_info,sizeof(wifi_info_t));   /* 保存本次结果供下次比较 */
}

/* 时间同步 */
static void time_sync(void)
{
	rtc_date_t rtc_date={0};
	uint32_t resync_period=0;
	
	resync_period=TIME_SYNC_INTERVAL;             /* 正常情况下 1 小时同步一次 */
	
	date_info_t sntp_date = { 0 };                /* 经 ESP-AT 从 SNTP 服务器取回的日期时间 */
    if (!esp_at_get_time(&sntp_date))
    {
        LOG_W("get time failed\n");               /* 获取时间失败 */
		resync_period=SECONDS(1);                 /* 缩短重试间隔，尽快重新同步 */
		goto resync;
    }
    if (sntp_date.year < 2000)
	{
		LOG_W("invalid date formate\n");          /* 年份不合理，判定为无效时间 */
		resync_period=SECONDS(1);                 /* 缩短重试间隔，尽快重新同步 */
		goto resync;
	}
	/* 打印同步到的 SNTP 日期时间及星期 */
    LOG_I("%04u-%02u-%02u %02u:%02u:%02u (%s)\n",
        sntp_date.year, sntp_date.month, sntp_date.day, sntp_date.hour, sntp_date.minute, sntp_date.second,
        sntp_date.weekday == 1 ? "Monday":
        sntp_date.weekday == 2 ? "Tuesday":
        sntp_date.weekday == 3 ? "Wednesday":
        sntp_date.weekday == 4 ? "Thursday":
        sntp_date.weekday == 5 ? "Friday":
        sntp_date.weekday == 6 ? "Saturday":
        sntp_date.weekday == 7 ? "Sunday" : "Unknown");
	
/* 把同步到的 SNTP 时间写入 RTC */	
	
	rtc_date.year=sntp_date.year;
	rtc_date.month=sntp_date.month;
	rtc_date.day=sntp_date.day;
	rtc_date.hour=sntp_date.hour;
	rtc_date.minute=sntp_date.minute;
	rtc_date.second=sntp_date.second;
	rtc_date.weekday=sntp_date.weekday;
	rtc_set_time(&rtc_date);
	
resync:   /* 无论同步成功与否都会走到这里，类似汇编中的标签跳转 */
	xTimerChangePeriod(time_sync_timer,pdMS_TO_TICKS(resync_period),0);   /* 按新周期续期 */
}
/* 时钟走时刷新：读取 RTC 并重绘主页面时间（秒级） */
static void time_update(void)
{
	static rtc_date_t last_date={0};    /* 上次已重绘的日期，用于判断日期是否需要刷新 */

	rtc_date_t date;
	rtc_get_time(&date);                /* 读取 RTC 实时时间 */

	if(date.year<2020)                  /* RTC 尚未同步时年份异常，直接跳过 */
		return;

	main_page_redraw_time(&date);       /* 时间每秒都重绘 */
	if(last_date.year!=date.year
		|| last_date.month!=date.month
		|| last_date.day!=date.day)
	{
		main_page_redraw_date(&date);   /* 仅日期变化时重绘日期，减少刷新开销 */
	}

	memcpy(&last_date,&date,sizeof(rtc_date_t));   /* 保存本次结果供下次比较 */
}
/* 室内温湿度刷新：读取 AHT20 并重绘主页面温湿度 */
static void indoor_update(void)
{
	/* 缓存上一次的温湿度，初值取不可能的数值以保证首次必然刷新 */
	static float last_temperature=999.99f,last_humidity=999.99f;

	xTimerChangePeriod(indoor_update_timer,pdMS_TO_TICKS(INDOOR_UPDATE_INTERVAL),0);   /* 按新周期续期 */

	float temperature=0.0f,humidity=0.0f;
	if(!aht20_measure(&temperature,&humidity))
	{
		LOG_W("measure fail!!");
		return;
	}
	if(temperature==last_temperature && humidity==last_humidity)   /* 数值未变化，无需重绘 */
		return;

	last_humidity=humidity;
	last_temperature=temperature;

	LOG_I("Temperature: %.1f, Humidity: %.1f\n", temperature, humidity);

	main_page_redraw_indoor_temperature(temperature);
	main_page_redraw_indoor_humidity(humidity);
}
/* 室外天气刷新：HTTP 拉取心知天气数据、解析后重绘主页面 */
static void outdoor_update(void)
{
	/* 缓存上一次的天气信息，仅内容变化时才重绘 */
	static weather_info_t last_weather={0};

	xTimerChangePeriod(outdoor_update_timer,pdMS_TO_TICKS(OUTDOOR_UPDATE_INTERVAL),0);   /* 不等待 */

	/* 直接以 HTTP GET 方式访问天气接口，响应体由模块内部缓冲，未经过 AT 接收缓冲区 */
	static weather_info_t weather = { 0 };
	const char *weather_http_response = esp_at_get_http(WEATHER_URL);
	if (weather_http_response == NULL)
	{
		LOG_W("http error\n");        /* 访问接口失败 */
		return;
	}

	if (!seniverse_parse(weather_http_response, &weather))
	{
		LOG_W("parse failed\n");      /* 响应报文解析失败 */
		return;
	}
	if(memcmp(&weather,&last_weather,sizeof(weather_info_t))==0)   /* 与上次一致，无需重绘 */
		return;
	LOG_I("%s, %s, %d,%.1f\n", weather.City, weather.Weather,weather.Code, weather.Temperature);   /* 打印天气信息 */

	memcpy(&last_weather,&weather,sizeof(weather_info_t));   /* 保存本次结果供下次比较 */

	/* 重绘室外天气 */
	main_page_redraw_outdoor_weather_icon(weather.Code);
	main_page_redraw_outdoor_temperature(weather.Temperature);
}


/*********************************************** Main Work ***********************************************/

/* 队列中执行的任务函数原型（无参无返回值） */
typedef void(*app_job_t)(void);

/* 适配器：把无参任务函数包装成工作队列所需的带参回调形式 */
static void app_work(void* param)
{
	app_job_t job=(app_job_t)param;
	job();
}

/* 通用软件定时器回调：从定时器 ID 取出任务函数并投递到工作队列异步执行 */
static void mloop_timer_callback(TimerHandle_t timer)
{
	app_job_t job=(app_job_t)pvTimerGetTimerID(timer);
	workqueue_run(app_work,job);
}
/* 时钟走时定时器回调：实时性要求最高，直接在当前任务上下文执行，避免被其他模块阻塞 */
static void time_update_callback(TimerHandle_t timer)
{
	app_job_t job=(app_job_t)pvTimerGetTimerID(timer);
	job();
}
/* 应用主循环初始化：创建全部软件定时器，先立即刷新一轮，再启动定时器 */
void mainloop_init(void)
{
	/* 创建定时器 */
	time_sync_timer=xTimerCreate("time_sync",pdMS_TO_TICKS(500),pdFALSE,time_sync,mloop_timer_callback);
	time_update_timer=xTimerCreate("time_timer",pdMS_TO_TICKS(TIME_UPDATE_INTERVAL),pdTRUE,time_update,time_update_callback);
	wifi_update_timer=xTimerCreate("wifi_timer",pdMS_TO_TICKS(WIFI_UPDATE_INTERVAL),pdTRUE,wifi_update,mloop_timer_callback);
	indoor_update_timer=xTimerCreate("indoor_timer",pdMS_TO_TICKS(INDOOR_UPDATE_INTERVAL),pdTRUE,indoor_update,mloop_timer_callback);
	outdoor_update_timer=xTimerCreate("outdoor_timer",pdMS_TO_TICKS(OUTDOOR_UPDATE_INTERVAL),pdTRUE,outdoor_update,mloop_timer_callback);
	
	/* 先执行一遍所有功能 */
	workqueue_run(app_work, time_sync);
    workqueue_run(app_work, wifi_update);
    workqueue_run(app_work, indoor_update);
    workqueue_run(app_work, outdoor_update);
	
	/* 启动定时器 */
	xTimerStart(time_update_timer,0);   /* 不等待 */
	xTimerStart(wifi_update_timer,0);
	xTimerStart(indoor_update_timer,0);
	xTimerStart(outdoor_update_timer,0);
	xTimerStart(time_sync_timer,0);

	/* 应用已成功启动：清除 OTA 升级标志并复位启动计数，防止误判为启动失败 */
	ota_flag_init();
	ota_flag_clear_boot_count();
	ota_flag_clear_update();
}
