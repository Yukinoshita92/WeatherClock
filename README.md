# Weather Clock OTA · 基于 FreeRTOS 的桌面天气时钟

> 一个跑在 **STM32F407VET6** 上的桌面天气时钟，带 **串口 IAP + WiFi 云端 OTA 双链路固件升级**。
> 独立完成硬件驱动、RTOS 架构、BootLoader 与云端联调全链路。

[![MCU](https://img.shields.io/badge/MCU-STM32F407VET6-blue)]()
[![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS-green)]()
[![Cloud](https://img.shields.io/badge/Cloud-OneNET%20%2B%20COS-orange)]()
[![OTA](https://img.shields.io/badge/OTA-IAP%20%2B%20WiFi-red)]()

---

## 一、项目简介

一台插电即用的桌面时钟：**联网自动对时**，屏上显示时间日期、**心知天气**实况与天气图标，以及板载 **AHT20** 采集的室内温湿度。

它真正的难点不在"显示"，而在**长期免拆机升级**：设备装好后放在桌上，如果每次改一行代码都要插 ST-Link，这个项目就没有实用价值。所以本项目的核心是一套**自研双链路 OTA 升级系统**——

| 链路 | 用途 | 说明 |
|---|---|---|
| **串口 IAP（保底）** | 首次烧录 / 救砖 | 上电长按 KEY2 进入 recovery，PC 端脚本经 BootLoader 写 Flash |
| **WiFi 云端 OTA（主路径）** | 日常升级 | OneNET 下发通知 → 设备 HTTP 下载 → BootLoader 校验跳转 |

### 硬件清单

| 部件 | 型号 | 接口 / 说明 |
|---|---|---|
| 主控 | STM32F407VET6 | 168 MHz，512 KB Flash，192 KB RAM |
| 显示屏 | ST7789 | SPI + DMA，240×320 |
| 外部 Flash | W25Q64（8 MB） | SPI，用于 OTA 暂存区与备份区 |
| 温湿度传感器 | AHT20 | I2C1，室内环境 |
| EEPROM | AT24C02（256 B） | I2C2（BL 侧）/ I2C，存 OTA 关键状态 + CRC |
| WiFi 模块 | ESP32-C3（ESP-AT 固件） | USART2，AT 指令 |

---

## 二、项目解决的问题 & 用到的技术

### 2.1 解决的 5 个工程问题

**① 免拆机升级 → 双链路 OTA**
串口 IAP 作为保底（也是首次烧录方式），WiFi OTA 作为日常路径。两者共用同一套 BootLoader 与固件格式（XBIN 自描述头）。

**② 升级失败变砖 → 三重保险**
1. 升级前把旧固件备份到 W25Q64 备份区，校验失败自动回滚
2. **启动计数**：新固件连续 3 次未能成功跳转 → 判定失效，自动进 recovery
3. 上电长按 KEY2 主动进 recovery，用串口重烧
> 关键状态（升级标志 / 启动计数 / 失败计数 / 尝试版本号 / 备份有效标志）全部存 **AT24C02 EEPROM + CRC16**，替代掉上电即失的 BKUP 寄存器。

**③ 透传下载丢字节 → 环形缓冲 + 精确长度 + 总线互斥**
ESP 透传下载时，写 W25Q64 的几毫秒内没人读串口，USART2 无 FIFO/RX DMA，于是溢出丢字节（实测 8825/9184）。
解法：单字节 RXNE 中断持续写入 4 KB 环形缓冲；解析 HTTP `Content-Length` 按精确字节数下载；新增 `g_ota_busy` 互斥标志，OCR 期间禁止天气等任务抢占串口（否则乱码会把 GET 请求污染成 `400 Bad Request`）。

**④ 云端主动通知 → MQTT（OneNET）**
轮询 `version.json` 费流量且不及时。改为订阅 OneNET 物模型服务调用主题，平台下发 `{version, url}` 即触发升级；**保留 HTTP 轮询作降级兜底**（编译开关控制）。

**⑤ 资源受限下的 UI 实时性 → 工作队列 + 部分重绘**
4 个任务、栈 2~8 KB。用「软件定时器回调只投递 → 工作队列串行执行」两层调度替代 32 位事件位通知，扩展性更好；主页面只重绘变化区域，避免整屏刷新导致的卡顿。

### 2.2 技术栈

| 层次 | 技术 |
|---|---|
| MCU / RTOS | STM32F407VET6 · FreeRTOS（CMSIS-RTOS2）· STM32 HAL |
| 驱动 | 硬件 SPI + DMA（ST7789）· 软件 I2C（AT24C02 / AHT20）· SPI（W25Q64）· USART + TX DMA + 单字节 RXNE 中断 |
| 通信协议 | ESP-AT 指令集 · MQTT 3.1.1（非加密 1883）· HTTP/1.1 明文透传 · 自定义串口帧协议 |
| 云平台 | OneNET 新版 Studio（物模型服务调用）· 腾讯云 COS（固件托管） |
| 数据校验 | CRC16-XMODEM（帧校验）· CRC32/zlib（固件校验）· MAGIC 自描述头 |
| 工具链 | Keil MDK / ARM Compiler 5 · Python 3（标准库 + pyserial） |

### 2.3 关键技术设计

| 设计 | 要点 |
|---|---|
| **XBIN 自描述固件头** | 4 KB 头（256 B 结构体 + 保留字段）记录版本 / 大小 / 目标地址 / CRC32 / 机型 / 最低 BL 版本；固件版本号由打包脚本 `--version` 注入，BL 先校验头再决定是否搬运 |
| **BootLoader 跳转** | 校验栈顶地址与复位向量 → 关中断、反初始化 HAL、复位 SysTick → 设栈顶指针 → 重定向 VTOR → 取复位中断指针直接调用 |
| **版本防降级** | 版本串 `vX.Y.Z` 解析为 `u32 = maj<<16\|min<<8\|patch` 比较，**只有更新才升级**（原来 `strcmp==0` 才跳过，服务器回滚会导致设备降级） |
| **失败计数上限** | 失败计数只在"服务器出现更高版本"时清零；达上限即停止自动升级，避免"回滚→再下载→再失败"的无限重刷循环 |
| **OS 解耦** | 自研 4 级日志框架（编译期裁剪 + 模块 TAG + 可插拔后端）+ 工作队列（函数指针投递，可带参） |

---

## 三、目录结构

```
submit_v1.0_github/
├── BootLoader_f407vet6/              # BootLoader：32 KB
│   ├── App/                          #   App_BootLoader.c  三层架构应用层
│   │                                 #   bl_packet.c       串口帧协议状态机
│   │                                 #   magic_header.c    XBIN 头解析
│   │                                 #   crc16.c / crc32.c
│   ├── Int_driver/                   #   Int_W25Q64.c  Int_AT24C02.c  跳转
│   ├── Core/  Drivers/  MDK-ARM/
│   └── BootLoader_f407vet6.ioc
│
├── WeatherClock_FreeRTOS_HAL/        # App 主固件：起始 0x08009000
│   ├── App/                          #   业务层
│   │   ├── app_config.h              #   ★ 唯一配置入口（WiFi/天气/OneNET/固件托管）
│   │   ├── app.c                     #   两层调度：软件定时器 + 工作队列
│   │   ├── workqueue.c               #   函数指针工作队列（16 深）
│   │   ├── log.c / log.h             #   自研分级日志（4 级 + 编译期裁剪）
│   │   ├── wifi.c                    #   ESP-AT 上电初始化
│   │   ├── ota.c                     #   OTA 状态机：通知 / 下载 / 校验 / 复位
│   │   ├── page/                     #   欢迎页 / WiFi 页 / 主页面 / 错误页
│   │   ├── font/  image/             #   字模与图片资源
│   │   └── UI.c                      #   绘制任务（DMA + 互斥解耦）
│   ├── int_driver/                   #   驱动 / 协议辅助层
│   │   ├── esp_at.c / esp_at.h       #   AT 指令收发（DMA 发 + 中断收 + 二值信号量）
│   │   ├── esp_at_ota.c              #   透传下载 + 4 KB 环形缓冲
│   │   ├── esp_at_mqtt.c             #   MQTT AT 指令封装
│   │   ├── onenet_mqtt.c             #   OneNET 鉴权 / 订阅 / 通知解析
│   │   ├── aht20.c  st7789.c  Int_W25Q64.c  Int_AT24C02.c
│   │   ├── ota_flag.c                #   EEPROM 状态读写（含 CRC）
│   │   ├── ota_version.c             #   版本解析与比较（纯函数，可主机端单测）
│   │   └── magic_header.c  crc32.c  byteorder.h
│   ├── Middlewares/Third_Party/FreeRTOS/
│   ├── Core/  Drivers/  MDK-ARM/
│   └── WeatherClock_FreeRTOS_HAL.ioc
│
├── tool/                             # 上位机 / 构建工具（Python 3）
│   ├── serial_upgrader.py            #   串口 IAP 烧录（与 BL 帧协议一致）
│   ├── gen_onenet_token.py           #   离线生成 OneNET MQTT 鉴权 token
│   └── scripts/gen_magic_header.py   #   打包 XBIN 固件（bin → xbin）
│
├── LICENSE
└── README.md
```

---

## 四、如何使用

### 4.0 环境准备

| 项 | 要求 |
|---|---|
| IDE | Keil MDK 5，**ARM Compiler 5**（工程为 AC5） |
| 器件包 | Keil.STM32F4xx_DFP |
| Python | 3.8+（`serial_upgrader.py` 需 `pyserial`：`pip install pyserial`） |
| 硬件 | ST-Link（烧 BL）+ USB 转串口（串口 IAP） |

> **⚠️ 拿到工程后必改两处（见 4.2），否则编译不出可用的 bin。**

### 4.1 第一步：烧录 BootLoader

BootLoader 固定占据 Flash 起始，**ROM 地址无需修改**，用 ST-Link 直接烧录：

```
打开 BootLoader_f407vet6/MDK-ARM/BootLoader_f407vet6.uvprojx → 编译 → ST-Link 下载
```

### 4.2 第二步：编译 App（Keil 必改两项）

打开 `WeatherClock_FreeRTOS_HAL/MDK-ARM/WeatherClock_FreeRTOS_HAL.uvprojx`：

**① Target → IROM1 起始地址改为 `0x08009000`，Size `0x77000`**

因为 App 不在 Flash 起始（前面有 BL 32 KB + XBIN 头 4 KB）。代码里 `main.c` 也会重定向向量表：

```c
#define APP_START_ADDR  0x08009000
SCB->VTOR = APP_START_ADDR;      /* 重定向中断向量表 */
```

**② User → After Build 添加生成 bin 的指令（勾选 Run #1）**

```
<你的 ARM Compiler 5 安装路径>\bin\fromelf --bin -o "$L@L.bin" #L"
```

例如默认安装位置为：

```
C:\Keil_v5\ARM\ARM_Compiler_5.06u7\bin\fromelf --bin -o "$L@L.bin" #L"
```

> 本文档所在仓库已改为**默认安装路径**。若你的 Keil 装在别的路径（如 `D:\Keil_v5`），请改成本机实际路径 —— 这条命令负责产出 `app.bin`，没有它就无法打包 XBIN。

**③ 配置凭据**：编辑 `WeatherClock_FreeRTOS_HAL/App/app_config.h`，把 4 组占位值替换为你的真实值：

```c
#define CFG_WIFI_SSID      "YOUR_WIFI_SSID"      /* 你的 WiFi 名 */
#define CFG_WIFI_PWD       "YOUR_WIFI_PASSWORD"  /* 你的 WiFi 密码 */
#define CFG_WEATHER_KEY    "YOUR_SENIVERSE_KEY"  /* 心知天气 API 密钥 */
#define CFG_WEATHER_LOC    "YOUR_LOCATION_ID"    /* 心知天气城市编号 */
#define CFG_ONENET_PID     "YOUR_PRODUCT_ID"     /* OneNET 产品 ID */
#define CFG_ONENET_DEV     "YOUR_DEVICE_NAME"    /* OneNET 设备名 */
#define CFG_ONENET_TOKEN   "YOUR_ONENET_TOKEN"   /* 用 tool/gen_onenet_token.py 生成 */
#define CFG_OTA_HOST       "YOUR_BUCKET.cos.YOUR_REGION.myqcloud.com"  /* 存储桶域名，不含 http:// */
```

> 建议：改完后不要直接提交这个文件（仓库里放占位值是给他人复现用的）。
> 可用 `git update-index --skip-worktree WeatherClock_FreeRTOS_HAL/App/app_config.h` 让本地改动不进提交。

### 4.3 第三步：打包 XBIN 固件

把上一步产出的 `app.bin` 打包成带自描述头的 `.xbin`：

```bash
python tool/scripts/gen_magic_header.py \
    --input  WeatherClock_FreeRTOS_HAL/MDK-ARM/WeatherClock_FreeRTOS_HAL/WeatherClock_FreeRTOS_HAL.bin \
    --data-address 0x08009000 \
    --version v1.1.0 \
    --output WeatherClock_FreeRTOS_HAL/MDK-ARM/WeatherClock_FreeRTOS_HAL/WeatherClock_FreeRTOS_HAL_upgrade.xbin
```

| 参数 | 含义 |
|---|---|
| `--input` | App 编译产出的 raw bin |
| `--data-address` | **必须是 `0x08009000`**（App 起始地址，与 IROM1 一致） |
| `--version` | 注入 XBIN 头的版本号，**决定 OTA 新旧比较** |
| `--output` | 输出的 xbin 路径 |

### 4.4 第四步：串口 IAP 烧录（首次 / 救砖）

```bash
# 先确认串口号
python tool/serial_upgrader.py --list

# 上电时按住 KEY2 进 recovery，然后烧录
python tool/serial_upgrader.py COM9 \
    --firmware WeatherClock_FreeRTOS_HAL/MDK-ARM/WeatherClock_FreeRTOS_HAL/WeatherClock_FreeRTOS_HAL_upgrade.xbin \
    --baud 115200
```

> **⚠️ 波特率用 115200**（与 BootLoader 侧 `usart.c` 一致）。脚本 CLI 的默认值是 9600，务必显式指定。
> 烧录前后关闭串口助手，避免抢占串口。

### 4.5 第五步：配置云端（OneNET + 对象存储）

1. **OneNET 新版 Studio**：建产品 → 建物模型 → 建设备
   - 物模型添加一个**服务类型** `ota_upgrade`，参数 `version`(string)、`url`(string)
   - 订阅主题即为：`$sys/{产品ID}/{设备名}/thing/service/ota_upgrade/invoke`
2. **生成 token**：

```bash
# 方式一：改脚本配置区后运行
python tool/gen_onenet_token.py

# 方式二：用环境变量（推荐，凭据不落盘）
ONENET_PID=你的产品ID ONENET_AK=你的accessKey ONENET_DEV=你的设备名 python tool/gen_onenet_token.py
```
   输出的 `version=...&res=...&et=...&method=sha1&sign=...` 即 MQTT 密码，填入 `app_config.h` 的 `CFG_ONENET_TOKEN`。

3. **固件托管**：把 `app_upgrade.xbin` 上传到**对象存储**（如腾讯云 COS），拿到直链
   - 桶权限设 **公有读私有写**；用**默认域名**（免备案）；**勿开防盗链**
   - ⚠️ 本工程的下载链路是**明文 TCP 透传**，只能用 **http（80）** 访问；代码里 https 也会被归一到 80

### 4.6 第六步：下发升级

在 OneNET 控制台 → 设备详情 →「服务调用」→ 选 `ota_upgrade`，填：

```json
{ "version": "v1.1.0", "url": "http://你的存储桶域名/app_upgrade.xbin" }
```

设备端会：收到通知 → 判定版本更新 → HTTP 下载到 W25Q64 暂存区 → 长度/头/CRC 三重校验 → 置升级标志 → 复位 → BL 备份旧固件并搬运新固件 → 跳转运行。

### 4.7 工具速查

| 脚本 | 作用 | 典型命令 |
|---|---|---|
| `tool/scripts/gen_magic_header.py` | 打包 XBIN（bin + 4 KB 头） | `python tool/scripts/gen_magic_header.py --input app.bin --data-address 0x08009000 --version v1.1.0 --output xxx.xbin` |
| `tool/serial_upgrader.py` | 串口 IAP 烧录 / 列串口 | `python tool/serial_upgrader.py COM9 --firmware xxx.xbin --baud 115200` |
| `tool/gen_onenet_token.py` | 生成 OneNET MQTT token | `python tool/gen_onenet_token.py` |

---

## 五、Flash / 存储分区

### 主 Flash（内部，512 KB）

| 区域 | 地址 | 大小 | 说明 |
|---|---|---|---|
| BootLoader | `0x08000000 ~ 0x08007FFF` | 32 KB | 永不升级 |
| XBIN 头 | `0x08008000 ~ 0x08008FFF` | 4 KB | 版本 / 长度 / CRC32 / 目标地址 |
| App | `0x08009000 + 0x77000` | 476 KB | `LR_IROM1 0x08009000 0x77000`，`VTOR=0x08009000` |

> ⚠️ **已知限制**：XBIN 头与 App 同属 sector 2（`0x08008000~0x0800BFFF`），用 ST-Link 整扇区擦除会**连带擦掉 XBIN 头**，导致 BL 判定 App 无效。
> 因此 App 只能走**串口 IAP / WiFi OTA** 烧录。彻底解法是把 App 起点移到 `0x0800C000` 让头部独占 sector 2（涉及分区调整，尚未实施）。

### 外部 Flash W25Q64（8 MB）

| 区域 | 起始地址 | 说明 |
|---|---|---|
| OTA 暂存区 | `0x001000` | 下载的新固件（含 XBIN 头） |
| 备份区 | `0x200000` | 升级前备份的旧固件，供回滚 |

### AT24C02 EEPROM（OTA 状态区，`0x00~0x0F`）

| 偏移 | 内容 |
|---|---|
| `0x00` | MAGIC |
| `0x01` | 升级标志（`0x5A` = 需要升级） |
| `0x02` | 启动计数 |
| `0x03` | 失败计数 |
| `0x04`(4B) | 本次尝试的目标版本（`maj<<16\|min<<8\|patch`） |
| `0x08`(4B) | 旧固件长度 |
| `0x0C` | 备份有效标志 |
| `0x0E~0x0F` | CRC16（XMODEM） |

---

## 六、串口帧协议（BL ↔ 上位机）

| 项 | 约定 |
|---|---|
| 帧头 | 请求 `0xAA` / 响应 `0x55` |
| 操作码 | `0x01` INQUERY · `0x81` ERASE · `0x82` PROGRAM · `0x83` VERIFY · `0x21` RESET · `0x22` BOOT |
| 请求帧 | 帧头 + 操作码 + 长度(2B 大端) + 数据 + CRC16(2B 大端) |
| 响应帧 | 帧头 + 操作码 + 错误码 + 长度(2B 大端) + 数据 + CRC16(2B 大端) |
| 单帧上限 | 4 KB |
| 校验 | CRC16 = XMODEM（poly `0x1021`, init `0x0000`）；CRC32 = zlib |

---

## 七、踩坑记录（精选）

这些坑都在实机上踩过并修掉了，留档备查：

| 现象 | 根因 | 修复 |
|---|---|---|
| 固件头读到全 0，BL 判定 App 无效 | W25Q64 单次 Page Program 未按 256 B 页边界拆分，跨页回绕覆盖 | 驱动层按页拆分写入 |
| 下载少 359~700 字节 | 写 W25Q64 期间无人读串口，USART2 无 FIFO/RX DMA 导致 ORE 溢出 | 单字节中断写 4 KB 环形缓冲 + 按 `Content-Length` 精确下载 |
| OTA 下载卡死，COS 返回 `400 Bad Request` | 透传期间天气任务仍在发 AT 指令，混入 GET 请求报文 | 新增 `g_ota_busy` 互斥标志，整个下载窗口禁止其他任务发 AT |
| 版本检测总命中错误内容 | `strstr(resp,"version")` 命中了 AT 回显里的 `/version.json` | 锚点改为带引号的 `"\"version\""`，只匹配 JSON 键 |
| 时钟走时比真实时间快/慢 | 板载晶振 8 MHz，但 Keil 宏 `HSE_VALUE` 与 PLL_M 不匹配，实际系统时钟约 53 MHz | 校正 HSE_VALUE 与 PLL 参数 |
| 错误页一直黑屏不显示 | 初始化任务用 `while(1)` 死循环占 CPU，UI 任务拿不到队列 | 错误页改为非阻塞方式交 UI 任务绘制 |
| ST-Link 烧 App 后整机静默 | sector 2 被整扇区擦除，XBIN 头丢失 | 改用串口 IAP；后续可调整分区（见 §5 已知限制） |

---

## 八、许可

- 本项目源码采用 **MIT License**，见 [LICENSE](LICENSE)
- `Drivers/`、`Middlewares/` 下的 ST / ARM / FreeRTOS 代码遵循其原始许可（**BSD-3-Clause** 等），各目录自带 `LICENSE.txt`，请一并保留

---

## 九、备注

- 仓库中 `WeatherClock_FreeRTOS_HAL/App/app_config.h` 只含**占位值**，运行前请填入自己的凭据。
- 未包含演示固件二进制，可自行按 §4.2 / §4.3 编译打包。
