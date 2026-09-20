#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# PhotoPipeline — golden smoke driver (M1-T8: docs/m1-tasks.md §3.16 / §4.8;
# M2-T8: assertion level, docs/m2-tasks.md §4 T8 ②).
#
# 16 transcode pairs (9 M1 smoke pairs upgraded + 7 M2 assertion-level pairs). Every case runs
# `photopipeline --dev` into .cache/out-smoke/<case>/ and is then asserted by pp_verify against
# tests/golden/smoke/<case>.json (SCHEMA.md): pixels + metadata values (M2-T8 normalisation,
# #26 landed) + the WarningKind list from the .pp.json sidecar.
#
# usage: bash tests/golden/smoke.sh [BUILD_DIR]
#   BUILD_DIR   default <repo>/build/release   (must be configured with -DPP_BUILD_DEV=ON)
#   PP_BIN      default <BUILD_DIR>/photopipeline
#   PP_VERIFY   default <BUILD_DIR>/pp_verify
#   OUT_ROOT    default <repo>/.cache/out-smoke
# exit code = number of failed cases (0 = all OK)

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${1:-${BUILD_DIR:-$ROOT/build/release}}"
PP_BIN="${PP_BIN:-$BUILD/photopipeline}"
PP_VERIFY="${PP_VERIFY:-$BUILD/pp_verify}"
GOLDEN="$ROOT/tests/golden"
OUT_ROOT="${OUT_ROOT:-$ROOT/.cache/out-smoke}"

if [ ! -x "$PP_BIN" ]; then
    echo "smoke: photopipeline not executable: $PP_BIN" >&2
    echo "smoke: build directory '$BUILD' has no dev harness binary;" >&2
    echo "smoke: configure it with -DPP_BUILD_DEV=ON (a plain release build has no --dev)." >&2
    echo "用法: bash tests/golden/smoke.sh [BUILD_DIR]" >&2
    exit 2
fi
if [ ! -x "$PP_VERIFY" ]; then
    echo "smoke: pp_verify not executable: $PP_VERIFY" >&2
    echo "用法: bash tests/golden/smoke.sh [BUILD_DIR]" >&2
    exit 2
fi
if [ ! -d "$GOLDEN/base" ]; then
    echo "smoke: golden corpus missing: $GOLDEN/base (run tools/gen_corpus.sh)" >&2
    exit 2
fi

mkdir -p "$OUT_ROOT"

# case | input (relative to tests/golden) | expected output (relative to the case dir) | extra CLI args
# tiff16-lzw goes through --preset on purpose: it covers the preset-file branch of §3.15.
# The `--meta Exif.Image.Artist=…` writes are deliberate: they give every pair a metadata *value*
# assertion (M2-T8 ②) without depending on sidecar/toolchain-specific tags (e.g. OIIO's Software).
CASES=(
    "jpeg-lossy|base/photo.jpg|base/photo.jpg|--format jpeg --backend jpegli --bitdepth 8 --param distance=1.0 --meta Exif.Image.Artist=M2-T8-jpeg"
    "jxl-lossless|base/rgb8.png|base/rgb8.jxl|--format jxl --tech modular --lossless --meta Exif.Image.Artist=M2-T8-jxl"
    "png16-lossless|base/rgb16.png|base/rgb16.png|--format png --bitdepth 16 --meta Exif.Image.Artist=M2-T8-png16"
    "tiff16-lzw|base/rgb16.tif|base/rgb16.tif|--preset $GOLDEN/smoke/tiff16-lzw.preset.json --meta Exif.Image.Artist=M2-T8-tiff16"
    "webp-lossless|base/rgba8.png|base/rgba8.webp|--format webp --tech lossless --lossless --meta Exif.Image.Artist=M2-T8-webp"
    "heif-lossy|base/rgb8.png|base/rgb8.heic|--format heif --backend x265 --meta Exif.Image.Artist=M2-T8-heif"
    "avif-lossy|base/rgb8.png|base/rgb8.avif|--format avif --backend svt-av1 --meta Exif.Image.Artist=M2-T8-avif"
    "bmp-exact|base/rgb8.png|base/rgb8.bmp|--format bmp --bitdepth 24 --meta Exif.Image.Artist=M2-T8-bmp"
    "meta-artist|base/photo.jpg|base/photo.jpg|--format jpeg --backend jpegli --bitdepth 8 --param distance=1.0 --meta Exif.Image.Artist=M1-T8"
    "gray-webp|base/gray8.png|base/gray8.webp|--format webp --tech lossless --lossless --meta Exif.Image.Artist=M2-T8-gray"
    "alpha-jpeg|base/rgba8.png|base/rgba8.jpg|--format jpeg --backend jpegli --bitdepth 8 --meta Exif.Image.Artist=M2-T8-alpha"
    "depth-jpeg|base/rgb16.png|base/rgb16.jpg|--format jpeg --backend jpegli --bitdepth 8 --param distance=1.0 --meta Exif.Image.Artist=M2-T8-depth"
    "multipage-png|base/multi.tif|base/multi.png|--format png --bitdepth 8"
    "unicode-png|edge/测试📸unicode.png|edge/测试📸unicode.png|--format png --bitdepth 8 --meta Exif.Image.Artist=路径📸测试"
    "exif-roundtrip|meta/exif_full.jpg|meta/exif_full.jpg|--format jpeg --backend jpegli --bitdepth 8 --param distance=1.0"
    "metaonly-jpeg|meta/exif_full.jpg|meta/exif_full.jpg|--metadata-only --format jpeg --meta Exif.Image.Artist=M2-T8-only"
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
