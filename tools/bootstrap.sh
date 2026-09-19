#!/usr/bin/env bash
# PP-FROZEN(structure): fresh-machine bootstrap (everything repo-local)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/tools/versions.env"
mkdir -p "$ROOT/.toolchain" "$ROOT/.cache/vcpkg-binary"
if python3 -m venv "$ROOT/.toolchain/venv" 2>/dev/null; then
    "$ROOT/.toolchain/venv/bin/pip" --no-cache-dir install aqtinstall
    AQT=("$ROOT/.toolchain/venv/bin/aqt")
else
    pip3 --no-cache-dir install --target "$ROOT/.toolchain/pylibs" aqtinstall
    AQT=(env "PYTHONPATH=$ROOT/.toolchain/pylibs" python3 -m aqt)
fi
mkdir -p "$ROOT/.cache/aqt-home"
HOME="$ROOT/.cache/aqt-home" "${AQT[@]}" install-qt linux desktop "${QT_VERSION}" linux_gcc_64 -O "$ROOT/.toolchain/Qt"
if [ ! -d "$ROOT/vcpkg" ]; then
    git clone https://github.com/microsoft/vcpkg "$ROOT/vcpkg"
fi
git -C "$ROOT/vcpkg" fetch --tags
git -C "$ROOT/vcpkg" checkout "${VCPKG_TAG}"
"$ROOT/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
cp -n "$ROOT/tools/env.sh.example" "$ROOT/tools/env.sh"
echo "OK — source tools/env.sh then: cmake --preset release && cmake --build --preset release && bash tools/gen_corpus.sh && ctest --preset release"
