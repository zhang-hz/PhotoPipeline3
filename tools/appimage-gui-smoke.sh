#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# PhotoPipeline AppImage GUI 实启烟测（M2-T18）
#
# 用法:
#   tools/appimage-gui-smoke.sh [AppImage 路径] [--shot <PNG 路径>] [--wait <秒,默认8>]
#
# 做四件事并**原样打印证据**（供报告引用）:
#   ① 在真实显示下启动 AppImage（不经 offscreen；DISPLAY 保持环境原样）
#   ② 轮询 ≤N 秒: 进程是否存活（pgrep -f usr/bin/photopipeline）
#   ③ 窗口是否存在: xprop -root _NET_CLIENT_LIST → 逐窗口 WM_NAME/_NET_WM_PID/WM_CLASS
#      （无 wmctrl/xdotool 时的等价手段）；辅以 xwininfo -root -tree
#   ④ 截图: xwd/import/grim 等外部工具不可用时，用 python3 Xlib + PIL 抓根窗口并
#      按窗口几何裁剪（本机实测可用），输出 PNG 尺寸与字节数
# 结束一律 kill 子进程。退出码: 0 = 进程存活且找到窗口; 1 = 否则。
set -uo pipefail

APPIMAGE="${1:-dist/PhotoPipeline-0.1.0-x86_64.AppImage}"
shift || true
SHOT=""
WAIT=8
while [ $# -gt 0 ]; do
    case "$1" in
        --shot) SHOT="${2:-}"; shift 2 ;;
        --wait) WAIT="${2:-8}"; shift 2 ;;
        *) echo "appimage-gui-smoke: 未知参数: $1" >&2; exit 2 ;;
    esac
done
[ -x "$APPIMAGE" ] || { echo "appimage-gui-smoke: 不可执行: $APPIMAGE" >&2; exit 2; }
echo "== GUI 烟测: $APPIMAGE"
echo "== DISPLAY=[${DISPLAY:-unset}] WAYLAND_DISPLAY=[${WAYLAND_DISPLAY:-unset}] XDG_SESSION_TYPE=[${XDG_SESSION_TYPE:-unset}]"

LOG="$(mktemp "${TMPDIR:-/tmp}/pp-gui-smoke.XXXXXX.log")"
"$APPIMAGE" >"$LOG" 2>&1 &
APP_PID=$!
echo "== 已启动（AppRun pid=$APP_PID），等待窗口 ≤${WAIT}s"

WIN=""
for i in $(seq 1 "$WAIT"); do
    sleep 1
    if ! kill -0 "$APP_PID" 2>/dev/null; then
        echo "== 失败: 启动 ${i}s 内进程已退出"
        echo "---- 启动输出（原样）:"; sed 's/^/  /' "$LOG"
        wait "$APP_PID" 2>/dev/null; echo "== exit=$?"
        rm -f "$LOG"; exit 1
    fi
    list="$(xprop -root _NET_CLIENT_LIST 2>/dev/null | sed 's/.*# //')"
    for w in ${list//,/ }; do
        name="$(xprop -id "$w" WM_NAME 2>/dev/null)"
        case "$name" in *PhotoPipeline*) WIN="$w"; break ;; esac
    done
    [ -n "$WIN" ] && { echo "== ${i}s 时检测到窗口"; break; }
done

echo
echo "-- ① 进程:"
pgrep -af 'usr/bin/photopipeline' | sed 's/^/  /' || echo "  （无）"
echo "-- ② _NET_CLIENT_LIST（原样）:"
xprop -root _NET_CLIENT_LIST 2>&1 | sed 's/^/  /'
if [ -n "$WIN" ]; then
    echo "-- ③ 命中窗口 $WIN 属性（原样）:"
    xprop -id "$WIN" WM_NAME WM_CLASS _NET_WM_PID _NET_WM_STATE 2>&1 | sed 's/^/  /'
    echo "-- ③b xwininfo（原样，取 Map State/几何）:"
    xwininfo -id "$WIN" 2>&1 | grep -E 'Map State|Absolute upper-left|Width|Height|Window id' | sed 's/^/  /'
else
    echo "-- ③ 未找到 WM_NAME 含 PhotoPipeline 的窗口；xwininfo -root -tree 摘要:"
    xwininfo -root -tree 2>&1 | grep -iE 'photopipeline' | sed 's/^/  /' || echo "  （无匹配）"
fi

if [ -n "$SHOT" ] && [ -n "$WIN" ]; then
    echo "-- ④ 截图 → $SHOT"
    mkdir -p "$(dirname "$SHOT")"
    # 外部工具（import/grim/gnome-screenshot/scrot/xwd）本机均无 → python3 Xlib+PIL 兜底。
    # 注意: 本机 XWayland（DISPLAY=:0）**拒绝对 root 窗口 GetImage**（BadMatch，
    # 已实测），故优先直接抓**应用窗口**（实测可用），失败再退回 root+crop。
    WIN_ID="$WIN" SHOT="$SHOT" python3 - <<'PY' 2>&1 | sed 's/^/  /'
import os
from Xlib import display, X
from PIL import Image
d = display.Display()
root = d.screen().root
wid = int(os.environ["WIN_ID"], 16)
win = d.create_resource_object("window", wid)
shot = os.environ["SHOT"]
g = win.get_geometry()
try:                                   # 首选: 直接抓窗口（XWayland 下可用）
    im = win.get_image(0, 0, g.width, g.height, X.ZPixmap, 0xffffffff)
    img = Image.frombytes("RGB", (g.width, g.height), im.data, "raw", "BGRX")
    img.save(shot)
    print(f"method=window.get_image size={img.size[0]}x{img.size[1]}")
except Exception as e:
    print(f"窗口截图失败（{type(e).__name__}: {e}）→ 退回 root 裁剪")
    rg = root.get_geometry()
    im = root.get_image(0, 0, rg.width, rg.height, X.ZPixmap, 0xffffffff)
    img = Image.frombytes("RGB", (rg.width, rg.height), im.data, "raw", "BGRX")
    tr = win.translate_coords(root, 0, 0)
    x, y = -tr.x, -tr.y
    img.crop((max(0, x), max(0, y), min(rg.width, x + g.width), min(rg.height, y + g.height))).save(shot)
    print(f"method=root.get_image+crop size={g.width}x{g.height}")
PY
    if [ -f "$SHOT" ]; then
        echo "  文件: $SHOT  尺寸: $(python3 -c "from PIL import Image; im=Image.open('$SHOT'); print('%dx%d' % im.size)" 2>/dev/null)  字节数: $(stat -c '%s' "$SHOT") B  sha256: $(sha256sum "$SHOT" | cut -c1-16)…"
    fi
else
    echo "-- ④ 未截图（无窗口或未指定 --shot）"
fi

echo
echo "-- 启动输出（原样，stderr 合并）:"
sed 's/^/  /' "$LOG"

# 清理顺序: 先结束真正的应用进程（避免 AppRun 监督分支把它留下），再结束 AppRun
pkill -TERM -f '[u]sr/bin/photopipeline' 2>/dev/null
sleep 1
kill -TERM "$APP_PID" 2>/dev/null
sleep 1
pkill -KILL -f '[u]sr/bin/photopipeline' 2>/dev/null
kill -KILL "$APP_PID" 2>/dev/null
wait "$APP_PID" 2>/dev/null
rm -f "$LOG"
if [ -n "$WIN" ]; then echo "== 结论: PASS（进程存活 + 窗口存在）"; exit 0; fi
echo "== 结论: FAIL（无窗口）"; exit 1
