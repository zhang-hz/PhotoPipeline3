# PhotoPipeline

批量像素级转码器 + 元数据手术台（batch pixel-level transcoder with metadata surgery），GPL-3.0-or-later。

M0 只交付仓库骨架、冻结接口、链接探针与金标语料；UI 是一个空窗口（offscreen 冒烟用）。

## 构建（四步）

```bash
# 1. 新机器一键引导：aqtinstall 装 Qt、vcpkg 钉死 tag、生成 tools/env.sh
bash tools/bootstrap.sh

# 2. 载入环境（工具链全部在仓库内：.toolchain/、.cache/、vcpkg/）
source tools/env.sh

# 3. 配置 + 构建
cmake --preset release && cmake --build --preset release

# 4. 生成金标语料 + 跑测试
bash tools/gen_corpus.sh && ctest --preset release
```

其它预设：`cmake --preset dev`（ASan+UBSan，Debug）、`cmake --preset tsan`（TSan，仅 configure）。

## M0 工具

| 工具 | 用途 |
|---|---|
| `pp_linkprobe` | 链接探针：逐库运行时校验（lcms2 / exiv2(+BMFF) / OIIO 插件 / jpegli / libjxl / libheif HEVC+AV1 编码器 / libwebp）。输出 `PROBE <name> OK\|FAIL <detail>` 与尾部两行 `PLUGINS:`、`HEIF_ENCODERS:`；退出码 = FAIL 数 |
| `pp_mkfixtures` | fixture 生成与校验：`--make <dir>` 生成 exif_full.jpg / webp×2 / heif_exif.heic / avif_exif.avif / jxl_exif.jxl / cmyk.tif，`--verify <dir>` 逐项回读校验（`MADE`/`FIXTURE` 行，退出码 = FAIL 数，目录缺失 → SKIP 77） |
| `pp_spikes` | Spike 验证：`e` = lcms2 数值金值（sRGB→sRGB 恒等 ≤1e-5；白点→Lab(D50) L∈[99.5,100.5]）；`f --golden-root <dir>` = Exiv2 无损重写保真（SOS `FF DA` 之后字节完全一致 + OIIO 像素 hash 相等） |
| `tools/gen_corpus.sh` | 生成 `tests/golden/{base,edge,meta}` 语料并写出 `tests/golden/CHECKSUMS`（幂等；可 `OIIOTOOL=` / `PP_MKFIXTURES=` 覆盖工具路径） |

## 文档

- [docs/design.md](docs/design.md) — 架构与设计
- [docs/param-catalog.md](docs/param-catalog.md) — 参数目录（编码器参数语义来源）
- [docs/brainstorm-consensus.md](docs/brainstorm-consensus.md) — 共识记录
- [docs/m0-tasks.md](docs/m0-tasks.md) — M0 任务书（执行依据 / 冻结契约）

## 许可

GPL-3.0-or-later，全文见 [LICENSE](LICENSE)。
