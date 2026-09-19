#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# PhotoPipeline M0 golden corpus generator (frozen contract: docs/m0-tasks.md §12.4).
#
# usage: bash tools/gen_corpus.sh [GOLDEN_ROOT=tests/golden]
#   OIIOTOOL      default vcpkg_installed/x64-linux/tools/openimageio/oiiotool (env override)
#   PP_MKFIXTURES default build/release/pp_mkfixtures (env override)
#
# Idempotent: every output is overwritten on re-run.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GOLDEN_ROOT="${1:-${GOLDEN_ROOT:-$ROOT/tests/golden}}"
OIIOTOOL="${OIIOTOOL:-$ROOT/vcpkg_installed/x64-linux/tools/openimageio/oiiotool}"
PP_MKFIXTURES="${PP_MKFIXTURES:-$ROOT/build/release/pp_mkfixtures}"

mkdir -p "$GOLDEN_ROOT/base" "$GOLDEN_ROOT/edge" "$GOLDEN_ROOT/meta" "$GOLDEN_ROOT/real"

if [ ! -x "$OIIOTOOL" ]; then
    echo "gen_corpus: oiiotool not executable: $OIIOTOOL" >&2
    exit 1
fi
if [ ! -x "$PP_MKFIXTURES" ]; then
    echo "gen_corpus: pp_mkfixtures not executable: $PP_MKFIXTURES" >&2
    exit 1
fi

# pattern <out> <channels> <chnames> <depth> [checker cell size]
# oiiotool pattern syntax is validated against `oiiotool --help` on the installed build;
# the task book allows mechanical adaptation of these invocations.
pattern() {
    local out="$1" channels="$2" chnames="$3" depth="$4" cell="${5:-8}"
    "$OIIOTOOL" --pattern "checker:width=$cell:height=$cell" 64x64 "$channels" \
        --chnames "$chnames" --depth "$depth" -o "$out"
}

echo "gen_corpus: base/ (OIIOTOOL=$OIIOTOOL)"
pattern "$GOLDEN_ROOT/base/rgb8.png"    3 "R,G,B"   uint8
pattern "$GOLDEN_ROOT/base/rgb16.png"   3 "R,G,B"   uint16
pattern "$GOLDEN_ROOT/base/gray8.png"   1 "Y"       uint8
pattern "$GOLDEN_ROOT/base/gray16.png"  1 "Y"       uint16
pattern "$GOLDEN_ROOT/base/graya8.png"  2 "Y,A"     uint8
pattern "$GOLDEN_ROOT/base/rgba8.png"   4 "R,G,B,A" uint8
pattern "$GOLDEN_ROOT/base/rgba16.png"  4 "R,G,B,A" uint16
pattern "$GOLDEN_ROOT/base/rgb8.tif"    3 "R,G,B"   uint8
pattern "$GOLDEN_ROOT/base/rgb16.tif"   3 "R,G,B"   uint16
pattern "$GOLDEN_ROOT/base/gray16.tif"  1 "Y"       uint16
pattern "$GOLDEN_ROOT/base/photo.jpg"   3 "R,G,B"   uint8
pattern "$GOLDEN_ROOT/base/targa.tga"   3 "R,G,B"   uint8
pattern "$GOLDEN_ROOT/base/bmp24.bmp"   3 "R,G,B"   uint8

# multi.tif: two subimages.
"$OIIOTOOL" --pattern checker:width=8:height=8 64x64 3 --chnames R,G,B --depth uint8 \
    --pattern checker:width=4:height=4 32x32 3 --chnames R,G,B --depth uint8 \
    --siappend -o "$GOLDEN_ROOT/base/multi.tif"

# anim.gif: three subimages (animation).
"$OIIOTOOL" --pattern checker:width=8:height=8 64x64 3 --chnames R,G,B --depth uint8 \
    --pattern checker:width=4:height=4 64x64 3 --chnames R,G,B --depth uint8 \
    --pattern checker:width=2:height=2 64x64 3 --chnames R,G,B --depth uint8 \
    --siappend --siappend -o "$GOLDEN_ROOT/base/anim.gif"

# jxl8.jxl: written by oiiotool; if the jxl output plugin is unavailable the fixture is
# appended from pp_mkfixtures instead (which path was used is reported below).
JXL_VIA="oiiotool"
if ! "$OIIOTOOL" --pattern checker:width=8:height=8 64x64 3 --chnames R,G,B --depth uint8 \
        -o "$GOLDEN_ROOT/base/jxl8.jxl" 2>/dev/null; then
    JXL_VIA="pp_mkfixtures(meta/jxl_exif.jxl)"
fi

echo "gen_corpus: meta/ (PP_MKFIXTURES=$PP_MKFIXTURES)"
"$PP_MKFIXTURES" --make "$GOLDEN_ROOT/meta"

if [ "$JXL_VIA" != "oiiotool" ]; then
    cp -f "$GOLDEN_ROOT/meta/jxl_exif.jxl" "$GOLDEN_ROOT/base/jxl8.jxl"
fi
echo "gen_corpus: base/jxl8.jxl produced via $JXL_VIA"

echo "gen_corpus: edge/"
# cmyk.tif is produced by pp_mkfixtures (libtiff direct write, PHOTOMETRIC_SEPARATED).
cp -f "$GOLDEN_ROOT/meta/cmyk.tif" "$GOLDEN_ROOT/edge/cmyk.tif"
head -c 300 "$GOLDEN_ROOT/base/photo.jpg" > "$GOLDEN_ROOT/edge/corrupt_trunc.jpg"
: > "$GOLDEN_ROOT/edge/corrupt_zero.png"
cp -f "$GOLDEN_ROOT/base/rgb8.png" "$GOLDEN_ROOT/edge/测试📸unicode.png"

echo "gen_corpus: CHECKSUMS"
(cd "$GOLDEN_ROOT" && find base edge meta -type f | sort | xargs sha256sum > CHECKSUMS)

COUNT="$(find "$GOLDEN_ROOT/base" "$GOLDEN_ROOT/edge" "$GOLDEN_ROOT/meta" -type f | wc -l)"
echo "gen_corpus: OK - $COUNT fixtures in $GOLDEN_ROOT (CHECKSUMS written)"
