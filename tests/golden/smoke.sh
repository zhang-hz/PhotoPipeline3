#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# PhotoPipeline M1-T8 — golden smoke driver (docs/m1-tasks.md §3.16 / §4.8).
#
# 8 transcode pairs + 1 metadata pair. Every case runs `photopipeline --dev` into
# .cache/out-smoke/<case>/ and is then asserted by pp_verify against
# tests/golden/smoke/<case>.json (SCHEMA.md).
#
# usage: bash tests/golden/smoke.sh [BUILD_DIR]
#   PP_BIN      default <BUILD_DIR>/photopipeline   (must be configured with -DPP_BUILD_DEV=ON)
#   PP_VERIFY   default <BUILD_DIR>/pp_verify
#   OUT_ROOT    default <repo>/.cache/out-smoke
# exit code = number of failed cases (0 = all OK)

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${1:-${BUILD_DIR:-$ROOT/build/m1-t8}}"
PP_BIN="${PP_BIN:-$BUILD/photopipeline}"
PP_VERIFY="${PP_VERIFY:-$BUILD/pp_verify}"
GOLDEN="$ROOT/tests/golden"
OUT_ROOT="${OUT_ROOT:-$ROOT/.cache/out-smoke}"

if [ ! -x "$PP_BIN" ]; then
    echo "smoke: photopipeline not executable: $PP_BIN" >&2
    exit 2
fi
if [ ! -x "$PP_VERIFY" ]; then
    echo "smoke: pp_verify not executable: $PP_VERIFY" >&2
    exit 2
fi
if [ ! -d "$GOLDEN/base" ]; then
    echo "smoke: golden corpus missing: $GOLDEN/base (run tools/gen_corpus.sh)" >&2
    exit 2
fi

mkdir -p "$OUT_ROOT"

# case | input (relative to tests/golden) | expected output (relative to the case dir) | extra CLI args
# tiff16-lzw goes through --preset on purpose: it covers the preset-file branch of §3.15.
CASES=(
    "jpeg-lossy|base/photo.jpg|base/photo.jpg|--format jpeg --backend jpegli --bitdepth 8 --param distance=1.0"
    "jxl-lossless|base/rgb8.png|base/rgb8.jxl|--format jxl --tech modular --lossless"
    "png16-lossless|base/rgb16.png|base/rgb16.png|--format png --bitdepth 16"
    "tiff16-lzw|base/rgb16.tif|base/rgb16.tif|--preset $GOLDEN/smoke/tiff16-lzw.preset.json"
    "webp-lossless|base/rgba8.png|base/rgba8.webp|--format webp --tech lossless --lossless"
    "heif-lossy|base/rgb8.png|base/rgb8.heic|--format heif --backend x265"
    "avif-lossy|base/rgb8.png|base/rgb8.avif|--format avif --backend svt-av1"
    "bmp-exact|base/rgb8.png|base/rgb8.bmp|--format bmp --bitdepth 24"
    "meta-artist|base/photo.jpg|base/photo.jpg|--format jpeg --backend jpegli --bitdepth 8 --param distance=1.0 --meta Exif.Image.Artist=M1-T8"
)

pass=0
fail=0
printf '%-16s %-8s %s\n' "case" "result" "detail"
for entry in "${CASES[@]}"; do
    IFS='|' read -r name input out_rel args <<<"$entry"
    outdir="$OUT_ROOT/$name"
    log="$OUT_ROOT/$name.dev.log"
    rm -rf "$outdir"
    # shellcheck disable=SC2086
    "$PP_BIN" --dev "$GOLDEN/$input" --out "$outdir" --base "$GOLDEN" \
        --conflict overwrite --workers 1 $args >"$log" 2>&1
    rc=$?
    if [ "$rc" -ne 0 ]; then
        printf '%-16s %-8s --dev exit %d (log: %s)\n' "$name" "FAIL" "$rc" "$log"
        fail=$((fail + 1))
        continue
    fi
    actual="$outdir/$out_rel"
    if result="$("$PP_VERIFY" "$GOLDEN/smoke/$name.json" "$actual" 2>&1)"; then
        printf '%-16s %-8s %s\n' "$name" "PASS" "$result"
        pass=$((pass + 1))
    else
        printf '%-16s %-8s %s\n' "$name" "FAIL" "$result"
        fail=$((fail + 1))
    fi
done

total=$((pass + fail))
printf 'SMOKE total=%d pass=%d fail=%d (out: %s)\n' "$total" "$pass" "$fail" "$OUT_ROOT"
exit "$fail"
