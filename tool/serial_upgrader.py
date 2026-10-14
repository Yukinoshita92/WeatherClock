#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
serial_upgrader.py — STM32F4 BootLoader 串口固件升级工具
=========================================================

功能:
    通过串口把 .xbin 自描述固件升级到 STM32F407 上的 App 区。
    流程与 SerialUpgrader.exe 完全一致: 查询版本/MTU -> 擦除 ->
    写入 4KB 头部 -> 分帧写入固件数据 -> 校验 CRC32 -> 跳转运行。

协议 (与 BootLoader/bl_packet.c 逐字段一致):
    request : 0xAA | opcode(1) | len(2 大端) | payload(len) | crc16(2 大端)
    response: 0x55 | opcode(1) | errcode(1) | len(2 大端) | data(len) | crc16(2 大端)

    CRC16 : XMODEM, poly=0x1021, init=0x0000 (self-check: crc16("123456789")==0x31C3)
    CRC32 : zlib, init=0xFFFFFFFF, final xor 0xFFFFFFFF (python zlib.crc32 即此实现)

    操作码: INQUERY 0x01(子码0=版本,1=MTU)  ERASE 0x81  PROGRAM 0x82
            VERIFY 0x83  RESET 0x21  BOOT 0x22
    ERASE  payload = addr(4) + size(4)
    PROGRAM payload = addr(4) + size(4) + data(size)   (单帧数据 <= 4096)
    VERIFY payload = addr(4) + size(4) + crc32(4)

本工具只做一件事: 对指定串口上的 BootLoader 做固件升级。
不会扫描/触碰其它设备,不会改 BootLoader 或 App 的工程文件,无网络操作。

依赖: 仅 pyserial (pip install pyserial)。图形界面用标准库 tkinter。

用法:
    # 列出可用串口
    python serial_upgrader.py --list

    # 命令行升级 (CLI 模式, 会先打印确认)
    python serial_upgrader.py COM8 --firmware xxx.xbin --baud 9600

    # 跳过确认 (脚本化)
    python serial_upgrader.py COM8 --firmware xxx.xbin --baud 9600 --yes

    # 无参数或双击 -> 打开图形界面 (界面与 SerialUpgrader.exe 类似)
    python serial_upgrader.py

    # 自检 CRC 算法一致性 (可选, 打印两个标准 self-check 值)
    python serial_upgrader.py --selfcheck
"""

import argparse
import os
import struct
import sys
import time
import zlib

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("[错误] 缺少 pyserial,请先安装: pip install pyserial")
    sys.exit(1)


# ---------------------------------------------------------------------------
# 常量
# ---------------------------------------------------------------------------
REQ_HEADER  = 0xAA          # 请求帧头
RESP_HEADER = 0x55          # 响应帧头

OPCODE = {
    "INQUERY": 0x01,
    "RESET":   0x21,
    "BOOT":    0x22,
    "ERASE":   0x81,
    "PROGRAM": 0x82,
    "VERIFY":  0x83,
}

ERRCODE_TEXT = {
    0x00: "OK",
    0x01: "OPCODE 错误(未知操作码)",
    0x02: "OVERFLOW(数据溢出)",
    0x03: "TIMEOUT",
    0x04: "FORMAT(格式错误)",
    0x05: "VERIFY(校验失败)",
    0x06: "PARAM(参数错误)",
    0xFF: "UNKNOWN",
}

# 下位机 Flash 边界 (与 bl_packet.c / Int_bootloader_skip.h 一致)
FLASH_BASE    = 0x08000000
FLASH_END     = 0x08080000
BL_END_ADDR   = 0x08008000   # BL 禁止擦/写 < 该地址

MAGIC_XBIN    = 0x4D414749   # "MAGI"
HEADER_SIZE   = 4096         # xbin 头部固定 4KB

BAUD_PRESETS  = [9600, 19200, 38400, 115200, 230400, 460800, 921600]


# ---------------------------------------------------------------------------
# 异常
# ---------------------------------------------------------------------------
class UpgradeError(Exception):
    """升级过程中的可报告错误。"""


class CmdTimeout(UpgradeError):
    """命令无应答(超时)。"""


# ---------------------------------------------------------------------------
# CRC
# ---------------------------------------------------------------------------
def crc16_xmodem(data: bytes) -> int:
    """CRC16 XMODEM: poly=0x1021, init=0x0000。与下位机 crc16.c 一致。"""
    crc = 0x0000
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


def crc32_zlib(data: bytes) -> int:
    """CRC32 zlib,与下位机 crc32.c 一致。"""
    return zlib.crc32(data) & 0xFFFFFFFF


def selfcheck():
    """打印两个标准 self-check,验证本地 CRC 与下位机实现一致。"""
    c16 = crc16_xmodem(b"123456789")
    c32 = crc32_zlib(b"123456789")
    print("CRC16 XMODEM self-check: 0x%04X (期望 0x31C3) %s"
          % (c16, "OK" if c16 == 0x31C3 else "FAIL"))
    print("CRC32 zlib    self-check: 0x%08X (期望 0xCBF43926) %s"
          % (c32, "OK" if c32 == 0xCBF43926 else "FAIL"))
    return c16 == 0x31C3 and c32 == 0xCBF43926


# ---------------------------------------------------------------------------
# 帧构造 / 响应解析
# ---------------------------------------------------------------------------
def build_request(opcode: int, payload: bytes = b"") -> bytes:
    """0xAA | opcode | len(2 BE) | payload | crc16(2 BE)"""
    frame = bytes([REQ_HEADER, opcode]) + struct.pack(">H", len(payload)) + payload
    return frame + struct.pack(">H", crc16_xmodem(frame))


def parse_response(buf: bytes):
    """
    解析完整响应帧,返回 (opcode, errcode, data)。
    帧不完整/头不对/CRC 不匹配时返回 None。
    """
    if len(buf) < 7 or buf[0] != RESP_HEADER:
        return None
    opcode, errcode = buf[1], buf[2]
    length = struct.unpack(">H", buf[3:5])[0]
    if len(buf) < 5 + length + 2:
        return None
    data = buf[5:5 + length]
    crc_recv = struct.unpack(">H", buf[5 + length:7 + length])[0]
    if crc_recv != crc16_xmodem(buf[:5 + length]):
        return None
    return (opcode, errcode, data)


def be32(v: int) -> bytes:
    return struct.pack(">I", v & 0xFFFFFFFF)


def rd_be32(b: bytes, off: int) -> int:
    return struct.unpack(">I", b[off:off + 4])[0]


# ---------------------------------------------------------------------------
# 串口命令层
# ---------------------------------------------------------------------------
def send_command(ser, opcode: int, payload: bytes = b"",
                 extra_timeout: float = 5.0, log=None):
    """
    发送一帧命令并等待完整响应。

    超时 = 发送耗时 + extra_timeout。发送耗时 = 帧长*10/波特率,
    这样大帧(4KB 数据)在 9600 波特下也能等足时间。
    返回 (opcode, errcode, data)。CRC 错误 / 帧头错误 / 无应答均抛异常。
    """
    frame = build_request(opcode, payload)
    baud = ser.baudrate or 9600
    tx_time = len(frame) * 10.0 / baud

    ser.reset_input_buffer()           # 清掉陈旧字节,避免污染本次应答
    ser.write(frame)

    deadline = time.monotonic() + tx_time + extra_timeout
    buf = bytearray()
    header_found = False
    while time.monotonic() < deadline:
        b = ser.read(1)
        if not b:
            continue
        buf.append(b[0])
        if not header_found:
            if buf[-1] == RESP_HEADER:
                del buf[:-1]           # 丢弃 0x55 之前的杂散字节
                header_found = True
            continue
        if len(buf) >= 5:
            flen = struct.unpack(">H", bytes(buf[3:5]))[0]
            if len(buf) >= 5 + flen + 2:
                break

    if not header_found or len(buf) < 7:
        raise CmdTimeout("命令 0x%02X 无应答" % opcode)

    parsed = parse_response(bytes(buf))
    if parsed is None:
        raise UpgradeError("响应帧校验失败 (CRC/格式错误),原始: %s"
                           % buf.hex(" "))
    resp_op, errcode, data = parsed
    if resp_op != opcode:
        raise UpgradeError("响应操作码不匹配: 期望 0x%02X,收到 0x%02X"
                           % (opcode, resp_op))
    if errcode != 0:
        raise UpgradeError("命令 0x%02X 执行失败: %s"
                           % (opcode, ERRCODE_TEXT.get(errcode, hex(errcode))))
    return resp_op, errcode, data


# ---------------------------------------------------------------------------
# .xbin 固件解析与本地校验 (不触碰板子)
# ---------------------------------------------------------------------------
def parse_xbin(path):
    """
    解析并完整本地校验 .xbin 文件。
    返回 dict: version / hw_model / data_offset / data_address /
               data_length / data_crc32 / header_addr / header /
               payload / payload_crc32。
    任何校验不过都抛 UpgradeError,保证不把坏文件烧进去。
    """
    try:
        with open(path, "rb") as f:
            blob = f.read()
    except OSError as e:
        raise UpgradeError("无法读取固件文件 %s: %s" % (path, e))

    if len(blob) < HEADER_SIZE + 1:
        raise UpgradeError("文件太小,不是有效的 .xbin")

    header = blob[:HEADER_SIZE]
    payload = blob[HEADER_SIZE:]

    magic      = struct.unpack("<I", header[0x00:0x04])[0]
    data_offset = struct.unpack("<I", header[0x24:0x28])[0]
    data_address = struct.unpack("<I", header[0x28:0x2C])[0]
    data_length = struct.unpack("<I", header[0x2C:0x30])[0]
    data_crc32 = struct.unpack("<I", header[0x30:0x34])[0]
    hw_model   = struct.unpack("<I", header[0x34:0x38])[0]
    this_crc   = struct.unpack("<I", header[0xFC:0x100])[0]
    version    = header[0x60:0xE0].split(b"\x00", 1)[0].decode("ascii", "replace")

    if magic != MAGIC_XBIN:
        raise UpgradeError("不是 XBIN 固件 (magic 0x%08X != 'MAGI')" % magic)
    if this_crc != crc32_zlib(header[:0xFC]):
        raise UpgradeError("XBIN 头部 CRC 校验失败 (文件损坏)")
    if data_offset != HEADER_SIZE:
        raise UpgradeError("data_offset=%d,本工具仅支持 %d" % (data_offset, HEADER_SIZE))
    if len(payload) != data_length:
        raise UpgradeError("固件数据长度不符: 文件 %d 字节,头部声明 %d"
                           % (len(payload), data_length))
    if crc32_zlib(payload) != data_crc32:
        raise UpgradeError("固件数据 CRC32 校验失败 (文件损坏)")

    header_addr = data_address - data_offset
    if header_addr < BL_END_ADDR:
        raise UpgradeError("头部写入地址 0x%08X 落在 BootLoader 保护区,拒绝升级"
                           % header_addr)
    if header_addr + HEADER_SIZE + data_length > FLASH_END:
        raise UpgradeError("升级范围 [0x%08X, 0x%08X) 超出 Flash 上限 0x%08X"
                           % (header_addr, header_addr + HEADER_SIZE + data_length,
                              FLASH_END))

    return {
        "version": version,
        "hw_model": hw_model,
        "data_offset": data_offset,
        "data_address": data_address,
        "data_length": data_length,
        "data_crc32": data_crc32,
        "header_addr": header_addr,
        "header": header,
        "payload": payload,
        "payload_crc32": data_crc32,
    }


# ---------------------------------------------------------------------------
# 升级流程
# ---------------------------------------------------------------------------
def run_upgrade(ser, firm, chunk=4096, log=print, progress=None,
                skip_boot=False):
    """
    执行完整升级: 查询 -> 擦除 -> 写头部 -> 写数据 -> 校验 -> 跳转。
    每个命令失败会重试一次; 连续失败即中止并抛 UpgradeError。

    log:     回调(text) 输出日志
    progress: 回调(stage, done, total) 进度; stage 为 'ERASE'/'HEADER'/
              'DATA'/'VERIFY'/'BOOT', 数据阶段 done/total 为字节数
    skip_boot: True 时最后不自动跳转 App (仅烧录)
    """
    if not (0 < chunk <= HEADER_SIZE):
        raise UpgradeError("chunk 必须为 1..%d" % HEADER_SIZE)

    def _log(msg):
        log("[%s] %s" % (time.strftime("%H:%M:%S"), msg))

    def _cmd(op, payload, extra=5.0):
        # 单条命令,失败重试一次
        try:
            return send_command(ser, op, payload, extra_timeout=extra)
        except CmdTimeout:
            _log("  命令 0x%02X 超时,重试一次..." % op)
            return send_command(ser, op, payload, extra_timeout=extra)

    # 1. 查询版本与 MTU,确认链路
    _log("正在连接 BootLoader ...")
    _, _, ver = _cmd(OPCODE["INQUERY"], b"\x00")
    _log("BootLoader 版本: %s" % ver.decode("ascii", "replace"))
    _, _, mtu_b = _cmd(OPCODE["INQUERY"], b"\x01")
    mtu = rd_be32(mtu_b, 0) & 0xFFFF if len(mtu_b) == 4 else struct.unpack(">H", mtu_b)[0]
    _log("每帧最大负载(MTU): %d,数据块上限 %d" % (mtu, mtu - 8))

    # 2. 擦除: 覆盖 头部(4KB) + 固件数据 的范围
    erase_addr = firm["header_addr"]
    erase_size = HEADER_SIZE + firm["data_length"]
    _log("擦除 Flash [0x%08X, 0x%08X) ..." % (erase_addr, erase_addr + erase_size))
    if progress:
        progress("ERASE", 0, 0)
    _cmd(OPCODE["ERASE"], be32(erase_addr) + be32(erase_size), extra=30.0)

    # 3. 写 4KB 头部
    _log("写入头部 %d 字节 @ 0x%08X ..." % (HEADER_SIZE, erase_addr))
    if progress:
        progress("HEADER", 0, HEADER_SIZE)
    _cmd(OPCODE["PROGRAM"], be32(erase_addr) + be32(HEADER_SIZE) + firm["header"])

    # 4. 分帧写固件数据
    payload = firm["payload"]
    addr = firm["data_address"]
    total = len(payload)
    off = 0
    idx = 0
    _log("写入固件 %d 字节 @ 0x%08X,分 %d 帧 (每帧 %d) ..."
         % (total, addr, -(-total // chunk), chunk))
    while off < total:
        n = min(chunk, total - off)
        _cmd(OPCODE["PROGRAM"], be32(addr + off) + be32(n) + payload[off:off + n])
        off += n
        idx += 1
        if progress:
            progress("DATA", off, total)
        if idx % 10 == 0 or off >= total:
            _log("  数据帧 %d/%d (%d%%)" % (idx, -(-total // chunk),
                                           off * 100 // total))

    # 5. 校验: 头部 + 固件数据 (下位机从 Flash 现算 CRC32 对比)
    _log("校验头部 CRC32 ...")
    if progress:
        progress("VERIFY", 0, 1)
    _cmd(OPCODE["VERIFY"], be32(erase_addr) + be32(HEADER_SIZE)
         + be32(crc32_zlib(firm["header"])))
    _log("校验固件数据 CRC32 (0x%08X) ..." % firm["data_crc32"])
    _cmd(OPCODE["VERIFY"], be32(addr) + be32(total) + be32(firm["data_crc32"]))

    _log("升级完成,共写入 %d 字节,校验通过。" % (HEADER_SIZE + total))

    # 6. 跳转运行 (若未跳过)
    if skip_boot:
        _log("skip_boot 已设置,不自动跳转。")
        return
    if progress:
        progress("BOOT", 1, 1)
    try:
        send_command(ser, OPCODE["BOOT"], extra_timeout=3.0)
        _log("已下发跳转指令,App 即将运行。")
    except CmdTimeout:
        _log("未收到跳转确认(可能已跳转运行),请观察设备是否已启动 App。")


# ---------------------------------------------------------------------------
# CLI 入口
# ---------------------------------------------------------------------------
def cli_upgrade(port, firm_path, baud, chunk, yes):
    def log(msg):
        print(msg)
        sys.stdout.flush()

    firm = parse_xbin(firm_path)   # 先本地校验,失败直接报错不碰板子

    print("=" * 64)
    print("固件: %s" % os.path.basename(firm_path))
    print("版本: %s  硬件型号: %d" % (firm["version"], firm["hw_model"]))
    print("头部地址: 0x%08X  数据地址: 0x%08X" % (firm["header_addr"],
                                                 firm["data_address"]))
    print("数据长度: %d (%d KB)  CRC32: 0x%08X"
          % (firm["data_length"], firm["data_length"] // 1024, firm["data_crc32"]))
    print("串口: %s  波特率: %d  8-N-1" % (port, baud))
    print("将擦除 Flash [0x%08X, 0x%08X) 并写入新固件。"
          % (firm["header_addr"], firm["header_addr"] + HEADER_SIZE + firm["data_length"]))
    if firm["hw_model"] != 1:
        print("[警告] hw_model=%d 与本工程(1)不一致,下位机可能拒绝启动!" % firm["hw_model"])
    print("=" * 64)

    if not yes:
        try:
            r = input("确认升级? (y/N): ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            r = ""
        if r != "y":
            print("已取消。")
            return

    try:
        ser = serial.Serial(port, baud, bytesize=8, parity="N", stopbits=1,
                            timeout=0.1, rtscts=False, dsrdtr=False)
        # 保持 pyserial 默认 DTR/RTS,不主动拉低(避免触发板载 DTR->NRST 自动复位)
        time.sleep(0.2)
    except serial.SerialException as e:
        print("[错误] 无法打开串口 %s: %s" % (port, e))
        print("       可能被其它程序(如 SerialUpgrader.exe)占用,或串口号错误。")
        sys.exit(1)

    try:
        run_upgrade(ser, firm, chunk=chunk, log=log,
                    progress=lambda s, d, t: None)
        print("\n[成功] 固件升级完成并已跳转运行。")
    except UpgradeError as e:
        print("\n[失败] %s" % e)
        print("       若在数据阶段失败,再次运行即可重烧(可重复执行,不会损坏 BL)。")
        sys.exit(2)
    finally:
        try:
            ser.close()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# 图形界面 (tkinter, 仅标准库) —— 与 SerialUpgrader.exe 布局类似
# ---------------------------------------------------------------------------
def _gui_main():
    import tkinter as tk
    from tkinter import filedialog, messagebox

    try:
        import queue
    except ImportError:
        queue = None

    root = tk.Tk()
    root.title("串口固件升级工具 (STM32 BootLoader)")
    root.geometry("620x520")
    root.resizable(False, False)

    var_port = tk.StringVar()
    var_baud = tk.StringVar(value="9600")
    var_file = tk.StringVar()
    q = queue.Queue()
    running = [False]

    # ---------- 控件 ----------
    frm = tk.Frame(root, padx=12, pady=8)
    frm.pack(fill="both", expand=True)

    r1 = tk.Frame(frm)
    r1.pack(fill="x", pady=2)
    tk.Label(r1, text="串口:", width=8, anchor="w").pack(side="left")
    tk.OptionMenu(r1, var_port, "COM1").pack(side="left")

    def refresh_ports():
        ports = [p.device for p in list_ports.comports()]
        if not ports:
            ports = ["COM1"]
        menu = None
        for w in r1.winfo_children():
            if isinstance(w, tk.OptionMenu):
                menu = w
                break
        if menu is not None:
            menu.destroy()
        new_menu = tk.OptionMenu(r1, var_port, *ports)
        new_menu.pack(side="left")
        var_port.set(ports[0])

    tk.Button(r1, text="刷新", command=refresh_ports, width=6).pack(side="left", padx=6)

    r2 = tk.Frame(frm)
    r2.pack(fill="x", pady=2)
    tk.Label(r2, text="波特率:", width=8, anchor="w").pack(side="left")
    tk.OptionMenu(r2, var_baud, *[str(b) for b in BAUD_PRESETS]).pack(side="left")

    r3 = tk.Frame(frm)
    r3.pack(fill="x", pady=2)
    tk.Label(r3, text="固件:", width=8, anchor="w").pack(side="left")
    tk.Entry(r3, textvariable=var_file).pack(side="left", fill="x", expand=True)
    def pick():
        p = filedialog.askopenfilename(title="选择 .xbin 固件",
                                       filetypes=[("XBIN 固件", "*.xbin"),
                                                  ("所有文件", "*.*")])
        if p:
            var_file.set(p)
    tk.Button(r3, text="选择固件...", command=pick, width=10).pack(side="left", padx=6)

    txt = tk.Text(frm, height=16, state="disabled", font=("Consolas", 9))
    txt.pack(fill="both", expand=True, pady=6)

    prog = tk.ttk.Progressbar(frm, length=560, maximum=100)
    prog.pack(fill="x", pady=4)

    status = tk.Label(frm, text="就绪", anchor="w")
    status.pack(fill="x")

    btn = tk.Button(frm, text="开始升级", command=lambda: None, height=1)
    btn.pack(fill="x", pady=6)

    def log(text):
        q.put(text)

    # ---------- 升级线程 ----------
    def worker():
        port = var_port.get().strip()
        fpath = var_file.get().strip()
        baud = int(var_baud.get())
        try:
            firm = parse_xbin(fpath)
        except UpgradeError as e:
            log("[错误] %s" % e)
            q.put("__done__")
            return

        log("固件: %s  版本: %s" % (os.path.basename(fpath), firm["version"]))
        log("头部地址: 0x%08X  数据地址: 0x%08X" % (firm["header_addr"],
                                                  firm["data_address"]))
        log("数据长度: %d 字节  CRC32: 0x%08X"
            % (firm["data_length"], firm["data_crc32"]))
        if firm["hw_model"] != 1:
            log("[警告] hw_model=%d,与工程(1)不一致!" % firm["hw_model"])
        if not messagebox.askyesno("确认", "将擦除 Flash 并写入新固件,是否继续?"):
            log("已取消。")
            q.put("__done__")
            return

        try:
            ser = serial.Serial(port, baud, bytesize=8, parity="N", stopbits=1,
                                timeout=0.1, rtscts=False, dsrdtr=False)
            # 保持 pyserial 默认 DTR/RTS,不主动拉低(避免触发板载 DTR->NRST 自动复位)
            time.sleep(0.2)
        except serial.SerialException as e:
            log("[错误] 无法打开串口 %s: %s" % (port, e))
            q.put("__done__")
            return

        total_b = HEADER_SIZE + firm["data_length"]
        try:
            def pbar(stage, done, total):
                if stage in ("DATA", "HEADER"):
                    pct = int((done if stage == "DATA" else HEADER_SIZE)
                              / max(total_b, 1) * 100)
                elif stage == "ERASE":
                    pct = 1
                else:
                    pct = 99
                q.put(("__prog__", min(pct, 99)))

            run_upgrade(ser, firm, chunk=4096, log=log, progress=pbar)
            q.put(("__prog__", 100))
            log("[成功] 升级完成,已跳转运行。")
        except UpgradeError as e:
            log("[失败] %s" % e)
            log("       可再次运行重试(可重复烧写)。")
        finally:
            try:
                ser.close()
            except Exception:
                pass
            q.put("__done__")

    # ---------- 界面线程读队列 ----------
    def poll():
        try:
            while True:
                item = q.get_nowait()
                if item == "__done__":
                    running[0] = False
                    btn.config(state="normal")
                    status.config(text="完成")
                    continue
                if isinstance(item, tuple) and item[0] == "__prog__":
                    prog["value"] = item[1]
                    continue
                txt.config(state="normal")
                txt.insert("end", item + "\n")
                txt.see("end")
                txt.config(state="disabled")
        except (queue.Empty, Exception):
            pass
        root.after(50, poll)

    def start():
        if running[0]:
            return
        if not var_file.get().strip():
            messagebox.showwarning("提示", "请先选择 .xbin 固件文件")
            return
        running[0] = True
        btn.config(state="disabled")
        status.config(text="升级中...")
        prog["value"] = 0
        import threading
        threading.Thread(target=worker, daemon=True).start()

    btn.config(command=start)
    refresh_ports()
    root.after(50, poll)
    root.mainloop()


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="STM32F4 BootLoader 串口固件升级工具")
    ap.add_argument("port", nargs="?", help="串口号,如 COM8")
    ap.add_argument("--firmware", "-f", help=".xbin 固件文件路径")
    ap.add_argument("--baud", type=int, default=9600, help="波特率,默认 9600")
    ap.add_argument("--chunk", type=int, default=4096,
                    help="单帧数据块字节数,默认 4096 (最大 4096)")
    ap.add_argument("--yes", action="store_true", help="跳过擦除确认")
    ap.add_argument("--no-gui", action="store_true", help="强制命令行模式")
    ap.add_argument("--list", action="store_true", help="列出可用串口")
    ap.add_argument("--selfcheck", action="store_true", help="自检 CRC 算法")
    args = ap.parse_args()

    if args.selfcheck:
        sys.exit(0 if selfcheck() else 1)

    if args.list:
        ports = list_ports.comports()
        if not ports:
            print("未发现任何串口设备")
        for p in ports:
            print("  %s  -  %s" % (p.device, p.description))
        return

    # CLI 模式: 给出 串口 + 固件
    if args.port and args.firmware:
        cli_upgrade(args.port, args.firmware, args.baud, args.chunk, args.yes)
        return

    # 图形界面模式
    try:
        _gui_main()
    except ImportError:
        ap.print_help()
        print("\n[错误] 未安装 tkinter 且未提供命令行参数。")
        print("       请改用: python serial_upgrader.py COM8 --firmware xxx.xbin --baud 9600")
        sys.exit(1)


if __name__ == "__main__":
    main()
