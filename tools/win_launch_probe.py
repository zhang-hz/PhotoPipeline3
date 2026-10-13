#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""真机启动探针（Windows 真实窗口平台，M7-R2.4）。

背景（0.3.3 打不开 P0）：Frameless::nativeEventFilter 曾对每条原生消息调用 winId()，
窗口创建期（CreateWindowEx 同步投递 WM_NCCALCSIZE）重入 createWinId() 递归建窗直至
栈崩 —— 窗口全程不可见，用户侧现象 = 双击无反应。此缺陷 **offscreen 平台不可见**：
金样 / --ui-smoke / ctest 全部跑 offscreen，qwindows 真实建窗路径从不被触达。

本探针是该盲区的机械化判据（判据与本地同源，make_winzip 门禁第 6 条消费）：
  1) 启动被测 exe（真实 qwindows 平台）；
  2) 限时内出现**可见**主窗（标题 PhotoPipeline）；
  3) 进程顶层窗口总数不超阈 —— 递归建窗风暴守卫（正常 ~4 个：主窗 + Qt 观察窗 + IME）；
  4) 探测期内进程保持存活（启动即崩 = FAIL）。

冻结输出行（与 SMOKE / UI-SMOKE 同一冻结纪律，不得改写）：
    WIN-LAUNCH OK windows=<n> visible=<n>
失败输出 WIN-LAUNCH FAIL windows=<n> visible=<n> reason=<why>，退出码 1。

用法：python tools/win_launch_probe.py <photopipeline.exe> [--timeout 20] [--max-windows 32]
非 Windows 平台输出 WIN-LAUNCH SKIP 并退出 0（本探针只在 Windows 有意义）。
"""

from __future__ import annotations

import argparse
import ctypes
import os
import subprocess
import sys
import time

FROZEN_OK_PREFIX = "WIN-LAUNCH OK"  # 冻结行前缀（改动 = 破坏冻结面）
MAIN_TITLE = "PhotoPipeline"

user32 = ctypes.windll.user32  # type: ignore[attr-defined]


def enum_windows_for_pid(pid: int) -> tuple[int, int]:
    """返回 (该进程顶层窗口总数, 其中可见且标题==PhotoPipeline 的数量)。"""
    total = 0
    visible = 0
    target = ctypes.c_ulong(pid)
    seen = []

    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def cb(hwnd, _lparam):
        nonlocal total, visible
        proc_id = ctypes.c_ulong(0)
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(proc_id))
        if proc_id.value == target.value:
            total += 1
            if user32.IsWindowVisible(hwnd):
                buf = ctypes.create_unicode_buffer(256)
                user32.GetWindowTextW(hwnd, buf, 256)
                if buf.value == MAIN_TITLE:
                    visible += 1
            seen.append(hwnd)
        return True

    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return total, visible


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("--timeout", type=float, default=20.0)
    ap.add_argument("--max-windows", type=int, default=32)
    args = ap.parse_args()

    if os.name != "nt":
        print("WIN-LAUNCH SKIP non-windows")
        return 0

    exe = os.path.abspath(args.exe)
    if not os.path.isfile(exe):
        print(f"WIN-LAUNCH FAIL windows=0 visible=0 reason=missing-exe:{exe}")
        return 1

    proc = subprocess.Popen([exe], cwd=os.path.dirname(exe))
    deadline = time.monotonic() + args.timeout
    windows, visible = 0, 0
    reason = "timeout-no-visible-window"
    ok = False
    try:
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                reason = f"process-exited code={proc.returncode}"
                break
            windows, visible = enum_windows_for_pid(proc.pid)
            if windows > args.max_windows:
                reason = f"window-storm windows={windows}>{args.max_windows}"
                break
            if visible >= 1:
                ok = True
                reason = ""
                break
            time.sleep(0.25)
        else:
            windows, visible = enum_windows_for_pid(proc.pid)
    finally:
        proc.kill()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            pass

    if ok:
        # 冻结行（格式冻结）
        print(f"{FROZEN_OK_PREFIX} windows={windows} visible={visible}")
        return 0
    print(f"WIN-LAUNCH FAIL windows={windows} visible={visible} reason={reason}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
