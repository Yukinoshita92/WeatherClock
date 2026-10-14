#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
================================================================================
 OneNET (新版 Studio) MQTT 鉴权 token 生成脚本
================================================================================
作用：离线生成 OneNET 新版 Studio 的 MQTT 连接密码（token）。
      官方 token.exe 在 Win11 上常生成错误，故推荐用本脚本生成（可自定义有效期）。

用法：
  1. 修改下方【配置区】的 PRODUCT_ID / ACCESS_KEY / DEVICE_NAME
  2. 运行：python gen_onenet_token.py
  3. 复制输出的整串 token，用于 MQTT 连接 / AT 命令 / 代码宏

依赖：仅用 Python 标准库，无需额外安装。
================================================================================
"""

import base64
import hmac
import time
from urllib.parse import quote

# ==================== 配置区（请修改为你的实际值） ====================
PRODUCT_ID  = "YourID"        # OneNET 产品 ID（控制台 产品详情 里）
ACCESS_KEY  = "YourKey"     # 产品详情 → 访问控制（产品密钥）里的 accessKey
DEVICE_NAME = "YourNAME"        # 设备名；留空("")则生成"产品级 token"（更短、通用）

VALID_DAYS  = 730                 # token 有效期（天），默认 2 年
# ======================================================================


def gen_token(pid, access_key, dev=None, valid_days=730):
    """生成 OneNET MQTT 鉴权 token。

    pid        : 产品 ID
    access_key : 产品 accessKey（base64 字符串）
    dev        : 设备名；为 None 或 "" 时生成"产品级 token"（res 只含产品）
    valid_days : 有效期（天）
    """
    version = '2018-10-31'
    et = str(int(time.time()) + valid_days * 24 * 3600)   # 过期时间戳（秒）
    method = 'sha1'

    if dev is None or dev == "":
        res = 'products/%s' % pid                        # 产品级（更短）
    else:
        res = 'products/%s/devices/%s' % (pid, dev)       # 设备级（更安全）

    key = base64.b64decode(access_key)
    org = et + '\n' + method + '\n' + res + '\n' + version
    sign = base64.b64encode(hmac.new(key, org.encode(), 'sha1').digest()).decode()

    return 'version=%s&res=%s&et=%s&method=%s&sign=%s' % (
        version, quote(res, safe=''), et, method, quote(sign, safe=''))


def _is_placeholder(v):
    """判断是否仍为占位值（Your*/xxx/TODO/你的/< 开头即视为未填写）。"""
    s = v.strip()
    return s.lower().startswith(("your", "xxx", "todo")) or s.startswith(("你的", "<"))


if __name__ == "__main__":
    if _is_placeholder(PRODUCT_ID) or _is_placeholder(ACCESS_KEY) or _is_placeholder(DEVICE_NAME):
        print("！！请先在脚本【配置区】填写 PRODUCT_ID / ACCESS_KEY / DEVICE_NAME ！")
        raise SystemExit(1)

    token = gen_token(PRODUCT_ID, ACCESS_KEY, DEVICE_NAME, VALID_DAYS)

    print("=" * 70)
    print("OneNET token（MQTT 密码）:")
    print(token)
    print("=" * 70)
    print("长度    ：%d 字符" % len(token))
    print("有效期  ：%d 天（约 %.1f 年）" % (VALID_DAYS, VALID_DAYS / 365))
    print("token 类型：%s" % ("产品级（res 只含产品）" if not DEVICE_NAME else "设备级（res 含设备名）"))
    print()
    print("使用提示：")
    print("  1. 把上面整串 token 复制去 MQTT 连接（password）。")
    print("  2. 若拼进 AT+MQTTUSERCFG 后整条命令超 256 字节，把 DEVICE_NAME 改成 \"\"")
    print("     重新运行，得到更短的产品级 token。")
    print("  3. token 到期前需重新生成并替换。")
