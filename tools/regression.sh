#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# PhotoPipeline M2-T9 — full-corpus regression baseline (docs/m2-tasks.md §4 T9,
# exit criterion §8.6; docs/design.md §8.4 "回归基线：全语料 --dev 日志存档，
# debug 期间每修一 bug 全量 diff").
#
# usage:
#   bash tools/regression.sh [BUILD_DIR] [--update]
#
#   BUILD_DIR   default $BUILD_DIR or <repo>/build/release-dev; must contain the
#               dev harness binary `photopipeline` (built with -DPP_BUILD_DEV=ON).
#   --update    regenerate tools/baseline/golden.log from the current build
#               instead of diffing against it.
#
# exit codes:
#   0  zero diff — the normalized run log reproduces tools/baseline/golden.log
#   1  diff found — the first 40 diff lines are printed
#   2  environment missing (bad usage / no binary / no baseline / no corpus)
#
# ---------------------------------------------------------------------------
# WHAT IS RUN (all through the frozen dev harness `photopipeline --dev`, §3.15)
#   * full corpus, 27 inputs (tests/golden/{base:16, edge:4, meta:7}) x 8 formats
#     jpeg jxl png tiff webp bmp heif avif                      -> 8 invocations
#   * metadata-only (--metadata-only) x tests/golden/base:16 x 4 formats
#     jpeg png tiff webp  (the only formats supporting metadata-only)  -> 4
#   = 12 sequential invocations, 280 file slots.
#   tests/golden/real/ is deliberately NOT part of the baseline (user-supplied
#   samples, empty in-tree); tests/golden/smoke/ belongs to smoke.sh, not here.
#
# THE THREE PREMISES the baseline depends on (do not relax any of them):
#   1. workers=1      — single worker keeps the per-file order, the budget
#                       accounting and the encoder call sequence deterministic.
#   2. fixed out dirs — every run writes to .cache/regression/out-<run>; the dir
#                       is wiped first, so the run log path and the mirrored
#                       output paths (out=<...>) are identical on every run.
#   3. idempotent     — the whole script is safe to re-run: fresh out dirs,
#                       --conflict overwrite, no state carried between runs.
#   The script always runs from the repository root and passes *relative* input
#   paths, so no machine-specific absolute path can enter the baseline.
#
# NORMALIZATION RULES (applied to the spdlog run-*.log file and to the stdout
# table of every invocation, in this order):
#   N1  leading spdlog timestamp `HH:MM:SS.mmm ` -> removed
#   N2  thread id `[12345]` -> `[tid]`
#   N3  repository root prefix -> `<repo>` (guard; inputs are already relative)
#   N4  memory addresses (`0x` + >= 9 hex digits) -> `0x<addr>`; the 9-digit
#       floor keeps the libjxl hex version string `0x00002afa` verbatim
#   N5  `/tmp/<random segment>` -> `<tmp>` (guard)
#   N6  every `<field>_ms=<number>` -> `<field>_ms=<ms>` (decode/orient/color/
#       flatten/encode/metawrite/total/avg_file) and `throughput_mb_s=<number>`
#       -> `throughput_mb_s=<val>`
#   N7  `capacity_bytes=`/`budget_bytes=` -> `<bytes>`: the budget capacity is
#       derived from *currently available* RAM (min(available/2, 8 GiB)), so it
#       changes with machine load and must never enter the baseline
#   N8  the elapsed-conditional `budget report {…}` line is dropped entirely —
#       it only exists when a run took >= 1000 ms, which makes its presence
#       load-dependent; per-file `pixel budget acquired` lines keep the
#       unconditional budget accounting
#   N9  stdout table `file state bytes ms warnings` -> the ms column -> `<ms>`
#       (bytes, state and warnings stay verbatim)
#   Kept verbatim: level/stage/source, file names, parameter snapshot, warnings,
#   failure reasons and all summary counts (files/ok/failed/skipped/cancelled/
#   bytes) — i.e. exactly the "what changed" signal the baseline exists for.
#
# regenerate the baseline: bash tools/regression.sh <BUILD_DIR> --update

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASELINE="$ROOT/tools/baseline/golden.log"
OUT_ROOT_REL=".cache/regression"
OUT_ROOT="$ROOT/$OUT_ROOT_REL"
RAW="$OUT_ROOT/raw"
WORK="$OUT_ROOT/current.log"

FORMATS=(jpeg jxl png tiff webp bmp heif avif)
META_FORMATS=(jpeg png tiff webp)

usage() {
    sed -n '7,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

UPDATE=0
BUILD=""
CALLER_PWD="$PWD"
for arg in "$@"; do
    case "$arg" in
        --update) UPDATE=1 ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "regression: unknown option: $arg" >&2; usage >&2; exit 2 ;;
        *)
            if [ -n "$BUILD" ]; then
                echo "regression: too many arguments ('$BUILD' and '$arg')" >&2
                usage >&2
                exit 2
            fi
            BUILD="$arg"
            ;;
    esac
done
if [ -z "$BUILD" ]; then
    BUILD="${BUILD_DIR:-$ROOT/build/release-dev}"
fi
case "$BUILD" in
    /*) : ;;
    *) BUILD="$CALLER_PWD/$BUILD" ;;
esac
BIN="$BUILD/photopipeline"

# ---- environment checks (exit 2 = missing environment, not a regression) ----
if [ ! -x "$BIN" ]; then
    echo "regression: photopipeline not executable: $BIN" >&2
    echo "regression: pass a dev build dir or configure with -DPP_BUILD_DEV=ON." >&2
    echo "用法: bash tools/regression.sh [BUILD_DIR] [--update]" >&2
    exit 2
fi
if [ ! -d "$ROOT/tests/golden/base" ] || [ ! -d "$ROOT/tests/golden/edge" ] \
    || [ ! -d "$ROOT/tests/golden/meta" ]; then
    echo "regression: golden corpus missing under $ROOT/tests/golden (run tools/gen_corpus.sh)" >&2
    exit 2
fi
if [ "$UPDATE" -eq 0 ] && [ ! -f "$BASELINE" ]; then
    echo "regression: baseline missing: $BASELINE" >&2
    echo "regression: generate it once with: bash tools/regression.sh $BUILD --update" >&2
    exit 2
fi

cd "$ROOT" || exit 2

# ---- deterministic corpus listing (LC_ALL=C; relative paths) ----
CORPUS=()
while IFS= read -r f; do CORPUS+=("$f"); done < <(
    find tests/golden/base tests/golden/edge tests/golden/meta -type f -print | LC_ALL=C sort
)
BASE_CORPUS=()
while IFS= read -r f; do BASE_CORPUS+=("$f"); done < <(
    find tests/golden/base -type f -print | LC_ALL=C sort
)
if [ "${#CORPUS[@]}" -eq 0 ] || [ "${#BASE_CORPUS[@]}" -eq 0 ]; then
    echo "regression: corpus listing is empty" >&2
    exit 2
fi

rm -rf "$OUT_ROOT"
mkdir -p "$RAW"

# N1..N9 — see the header. Applied to the run log and to the stdout table.
normalize() {
    sed -E \
        -e 's/^[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3} //' \
        -e 's/^(\[[a-z]+\] )\[[0-9]+\] /\1[tid] /' \
        -e "s#${ROOT}#<repo>#g" \
        -e 's/0x[0-9a-fA-F]{9,}/0x<addr>/g' \
        -e 's#/tmp/[A-Za-z0-9._+-]+#<tmp>#g' \
        -e 's/([a-z_]+_ms)=[0-9]+(\.[0-9]+)?/\1=<ms>/g' \
        -e 's/throughput_mb_s=[0-9.]+/throughput_mb_s=<val>/g' \
        -e 's/(capacity_bytes|budget_bytes)=[0-9]+/\1=<bytes>/g' \
        | grep -v -F 'budget report {' \
        | awk '{
              if (NF >= 4 && ($2 == "ok" || $2 == "failed" || $2 == "skipped" || $2 == "cancelled")) {
                  $4 = "<ms>"; print; next
              }
              print
          }'
}

{
    printf '# ===========================================================================\n'
    printf '# PhotoPipeline M2-T9 regression baseline — normalized full-corpus --dev log\n'
    printf '# (docs/design.md §8.4; docs/m2-tasks.md §4 T9, exit criterion §8.6)\n'
    printf '#\n'
    printf '# matrix: 8 formats x 27 corpus inputs (base 16 + edge 4 + meta 7)\n'
    printf '#         + metadata-only (--metadata-only) x 4 formats x base 16\n'
    printf '# run shape: photopipeline --dev <corpus...> --out %s/out-<run> \\\n' "$OUT_ROOT_REL"
    printf '#            --base tests/golden --conflict overwrite --workers 1\n'
    printf '# prem.: workers=1, fixed+wiped out dirs, idempotent re-run (see tools/regression.sh)\n'
    printf '# norm.: timestamps/tid/*_ms/throughput/capacity+budget bytes -> placeholders;\n'
    printf '#        <repo> prefix, 0x<addr>, <tmp> segments; elapsed-conditional\n'
    printf '#        "budget report" lines dropped; stdout ms column -> <ms>\n'
    printf '# regenerate: bash tools/regression.sh <build_dir> --update\n'
    printf '# ===========================================================================\n'
} > "$WORK"

run_one() {
    local label="$1" kind="$2"
    shift 2
    local out_rel="$OUT_ROOT_REL/out-$label"
    local out="$ROOT/$out_rel"
    local raw_out="$RAW/stdout-$label.log"
    local logf rc
    rm -rf "$out"
    if [ "$kind" = "meta" ]; then
        LC_ALL=C "$BIN" --dev "${BASE_CORPUS[@]}" --out "$out_rel" --base tests/golden \
            --conflict overwrite --workers 1 --metadata-only "$@" >"$raw_out" 2>&1
    else
        LC_ALL=C "$BIN" --dev "${CORPUS[@]}" --out "$out_rel" --base tests/golden \
            --conflict overwrite --workers 1 "$@" >"$raw_out" 2>&1
    fi
    rc=$?
    logf="$(ls -1 "$out"/logs/run-*.log 2>/dev/null | LC_ALL=C sort | head -1)"
    {
        printf '### run %s rc=%d args=%s\n' "$label" "$rc" "$*"
        if [ -n "$logf" ]; then
            normalize < "$logf"
        else
            printf '(no run log written)\n'
        fi
        normalize < "$raw_out"
    } >> "$WORK"
    local summary
    summary="$(grep -m1 '^SUMMARY ' "$raw_out" 2>/dev/null)"
    printf 'regression: %-14s rc=%-3d %s\n' "$label" "$rc" "${summary#SUMMARY }"
}

printf 'regression: build  %s\n' "$BUILD"
printf 'regression: corpus %d files (base %d + edge 4 + meta 7)\n' \
    "${#CORPUS[@]}" "${#BASE_CORPUS[@]}"
printf 'regression: out    %s (wiped)\n' "$OUT_ROOT_REL"

for fmt in "${FORMATS[@]}"; do
    run_one "full-$fmt" full --format "$fmt"
done
for fmt in "${META_FORMATS[@]}"; do
    run_one "meta-$fmt" meta --format "$fmt"
done

lines_new="$(wc -l < "$WORK")"

if [ "$UPDATE" -eq 1 ]; then
    lines_old=0
    if [ -f "$BASELINE" ]; then
        lines_old="$(wc -l < "$BASELINE")"
        changed="$(diff -u "$BASELINE" "$WORK" | grep -cE '^[+-][^+-]' || true)"
        printf 'regression: --update: replacing baseline (%d lines -> %d lines, %s changed lines)\n' \
            "$lines_old" "$lines_new" "$changed"
    fi
    mkdir -p "$(dirname "$BASELINE")"
    cp "$WORK" "$BASELINE"
    printf 'regression: baseline written: tools/baseline/golden.log (%d lines)\n' "$lines_new"
    exit 0
fi

lines_base="$(wc -l < "$BASELINE")"
printf 'regression: normalized %d lines vs baseline %d lines\n' "$lines_new" "$lines_base"
d="$(diff -u "$BASELINE" "$WORK")"
rc=$?
if [ "$rc" -eq 0 ]; then
    printf 'regression: zero diff (baseline reproduced)\n'
    exit 0
fi
printf 'regression: DIFF FOUND (%s lines of unified diff, first 40 shown)\n' \
    "$(printf '%s\n' "$d" | wc -l)"
printf '%s\n' "$d" | head -40
exit 1
