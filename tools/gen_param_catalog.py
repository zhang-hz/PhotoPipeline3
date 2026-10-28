#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""M8 参数目录生成器（开发期工具；CI 不联网）。

输入：tools/param_catalog_specs/{x265,aom,svt,jxl}.json（人工维护的范围/标签/形态/规则）
输出：src/codecs/param_catalog/{x265,aom,svt,jxl}_catalog.inc（生成物，入库）
      src/codecs/param_catalog/anchors.inc（从 pinned 上游源码抽取的名字锚点，入库）

用法：
  python tools/gen_param_catalog.py              # 校验 + 写 .inc/anchors.inc
  python tools/gen_param_catalog.py --check      # 只校验 + 比较生成物是否最新
  python tools/gen_param_catalog.py --strict-counts  # 额外断言最终行数（Stage A4 门禁）

契约：docs/m8-encoder-params-and-ssimulacra2-design.md C1/C2；
      docs/m8-param-control-map.md（形态/角色/计数）；specs README 见同目录。
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC_DIR = ROOT / "tools" / "param_catalog_specs"
OUT_DIR = ROOT / "src" / "codecs" / "param_catalog"

GROUPS = {
    "质量/码率": "kGroupQuality",
    "分析工具": "kGroupAnalysis",
    "心理视觉": "kGroupPsy",
    "量化/变换": "kGroupQuant",
    "环路滤波": "kGroupLoop",
    "分块/线程": "kGroupThread",
    "码流/兼容性": "kGroupStream",
    "调试/实验": "kGroupDebug",
    "只读（上游固定）": "kGroupReadOnly",
}

TYPES = {"int": "Type::Int", "float": "Type::Float", "bool": "Type::Bool",
         "enum": "Type::Enum", "text": "Type::Text"}
FORMS = {"mode": "Form::Mode", "number": "Form::Number", "dropdown": "Form::Dropdown",
         "switch": "Form::Switch", "text": "Form::Text", "readonly": "Form::ReadOnly"}
ROLES = {"anchor": "Role::Anchor", "dependent": "Role::Dependent",
         "independent": "Role::Independent"}
SCOPES = {"settable": "Scope::Settable", "readonly": "Scope::ReadOnly",
          "forbidden": "Scope::Forbidden", "noeffect": "Scope::NoEffect"}
TECHS = {"both": "TechScope::Both", "vardct": "TechScope::Vardct",
         "modular": "TechScope::Modular"}

# (format, backend, filename, final row count)
BACKENDS = [
    ("heif", "x265", "x265.json", 40),
    ("avif", "libaom", "aom.json", 57),
    ("avif", "svt-av1", "svt.json", 3),
    ("jxl", "libjxl", "jxl.json", 16),
]

X265_FORBIDDEN = {
    "annexb", "input-csp", "total-frames", "ctu", "log-level", "log", "input-res",
    "pools", "frame-threads", "preset", "tune", "profile", "qpfile", "zonefile",
    "abr-ladder", "svt", "svt-av1", "nalu-file", "csv", "stats", "lambda-file",
    "analysis-save", "analysis-load", "film-grain", "dhdr10-info", "fovea-gaze-file",
    "zones", "bitrate", "vbv-maxrate", "vbv-bufsize", "vbv-init", "uhd-bd",
    "dolby-vision-profile", "intra-refresh", "pic-struct",
}
AOM_FORBIDDEN = {
    "input-chroma-subsampling-x", "input-chroma-subsampling-y", "denoise-noise-level",
    "denoise-block-size", "enable-dnl-denoising", "passes", "film-grain-testing",
    "film-grain-table", "film-grain", "tune", "end-usage", "usage", "profile",
    "bit-depth", "lag-in-frames", "kf-max-dist", "kf-min-dist",
    "full-still-picture-hdr", "resize-mode", "superres-mode", "superres-denominator",
    "superres-kf-denominator", "superres-qthresh", "superres-kf-qthresh", "vmaf",
}
JXL_KEYS_NO_PREFIX = True

KEY_RE = re.compile(r"^[a-z0-9_][a-z0-9_:.\-]*$")
ENGINE_RE = re.compile(r"^[a-z0-9][a-z0-9\-]*$")


def x265_engine_ok(engine: str, anchors: set[str]) -> bool:
    """x265_param_parse 通用 `no-` 前缀：no-deblock 等价于 deblock=false。"""
    if not anchors:
        return True
    if engine in anchors:
        return True
    return engine.startswith("no-") and engine[3:] in anchors


class SpecError(Exception):
    pass


def esc(value: str) -> str:
    out = value.replace("\\", "\\\\").replace('"', '\\"')
    out = out.replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
    return out


def cpp_value(value) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return f"int64_t({value})"
    if isinstance(value, float):
        return f"double({value!r})"
    if isinstance(value, str):
        return f'std::string("{esc(value)}")'
    raise SpecError(f"choice value type unsupported: {type(value)!r}")


def check(cond: bool, message: str) -> None:
    if not cond:
        raise SpecError(message)


def load_json(path: Path) -> dict:
    check(path.is_file(), f"missing spec file: {path}")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SpecError(f"{path.name}: invalid JSON: {exc}") from exc


def find_upstream_x265() -> Path | None:
    candidates = sorted((ROOT / "vcpkg" / "buildtrees" / "x265" / "src").glob("*/source/common/param.cpp"))
    return candidates[-1] if candidates else None


def find_upstream_aom() -> Path | None:
    candidates = sorted((ROOT / "vcpkg" / "buildtrees" / "aom" / "src").glob("*/av1/arg_defs.c"))
    if not candidates:
        return None
    for c in candidates:
        changelog = c.parent.parent / "CHANGELOG"
        if changelog.is_file():
            try:
                if "v3.15" in changelog.read_text(encoding="utf-8", errors="ignore")[:400]:
                    return c
            except OSError:
                pass
    return candidates[-1]


def extract_x265() -> list[str]:
    path = find_upstream_x265()
    if path is None:
        return []
    text = path.read_text(encoding="utf-8", errors="ignore")
    start = text.find("int x265_param_parse")
    check(start >= 0, "x265: x265_param_parse not found")
    end = text.find("\n}\n", start)
    region = text[start:] if end < 0 else text[start:end]
    names: list[str] = []
    for m in re.finditer(r'OPT2?\(\s*"([^"]+)"(?:\s*,\s*"([^"]+)")?', region):
        names.append(m.group(1))
        if m.group(2):
            names.append(m.group(2))
    return sorted(set(names))


def extract_aom() -> list[str]:
    path = find_upstream_aom()
    if path is None:
        return []
    text = path.read_text(encoding="utf-8", errors="ignore")
    return sorted(set(re.findall(r'ARG_DEF(?:_ENUM)?\(\s*NULL\s*,\s*"([^"]+)"', text)))


def write_anchors(x265: list[str], aom: list[str]) -> str:
    def array(name: str, names: list[str]) -> list[str]:
        lines = [f"inline constexpr const char *{name}[] = {{"]
        for i in range(0, len(names), 6):
            lines.append("    " + ", ".join(f'"{esc(n)}"' for n in names[i:i + 6]) + ",")
        lines.append("};")
        count_name = (name[:-1] if name.endswith("s") else name) + "Count"
        lines.append(f"inline constexpr std::size_t {count_name} = sizeof({name}) / sizeof({name}[0]);")
        return lines

    lines = [
        "// SPDX-License-Identifier: GPL-3.0-or-later",
        "// GENERATED by tools/gen_param_catalog.py — do not edit.",
        "// 上游参数名锚点：x265 4.3 x265_param_parse / libaom 3.15.1 arg_defs.c",
        "#pragma once",
        "#include <cstddef>",
        "",
        "namespace pp::catalog {",
        "",
    ]
    lines += array("kX265Anchors", x265)
    lines.append("")
    lines += array("kAomAnchors", aom)
    lines.append("")
    lines.append("} // namespace pp::catalog")
    lines.append("")
    return "\n".join(lines)


def validate_spec(spec: dict, filename: str, expected_format: str, expected_backend: str,
                  x265_anchors: set[str], aom_anchors: set[str], seen: set[str]) -> dict:
    check(spec.get("format") == expected_format,
          f"{filename}: format must be {expected_format!r}, got {spec.get('format')!r}")
    check(spec.get("backend") == expected_backend,
          f"{filename}: backend must be {expected_backend!r}, got {spec.get('backend')!r}")
    rows = spec.get("rows")
    check(isinstance(rows, list), f"{filename}: rows must be a list")
    for i, row in enumerate(rows):
        ctx = f"{filename}[{i}]"
        check(isinstance(row, dict), f"{ctx}: row must be object")
        for field in ("key", "label", "type", "form", "role", "scope", "group", "reason"):
            check(isinstance(row.get(field), str) and row[field] != "", f"{ctx}: missing {field}")
        key = row["key"]
        check(KEY_RE.match(key) is not None, f"{ctx}: bad key {key!r}")
        check(key not in seen, f"{ctx}: duplicate key {key!r}")
        seen.add(key)

        typ = row["type"]
        check(typ in TYPES, f"{ctx}: bad type {typ!r}")
        form = row["form"]
        check(form in FORMS, f"{ctx}: bad form {form!r}")
        role = row["role"]
        check(role in ROLES, f"{ctx}: bad role {role!r}")
        scope = row["scope"]
        check(scope in SCOPES, f"{ctx}: bad scope {scope!r}")
        check(scope in ("settable", "readonly"), f"{ctx}: Stage A only settable/readonly")
        tech = row.get("tech", "both")
        check(tech in TECHS, f"{ctx}: bad tech {tech!r}")
        check(row["group"] in GROUPS, f"{ctx}: bad group {row['group']!r}")
        check(isinstance(row.get("tooltip"), str), f"{ctx}: tooltip must be string")
        if scope == "settable":
            check(row["tooltip"] != "", f"{ctx}: settable row needs tooltip")
        rule_ids = row.get("rules", "")
        check(isinstance(rule_ids, str), f"{ctx}: rules must be string")

        engine = row.get("engine", "")
        check(isinstance(engine, str), f"{ctx}: engine must be string")

        # key / engine 作用域
        if expected_backend == "x265":
            prefix = "x265:" if scope == "settable" else "__x265:"
            check(key.startswith(prefix), f"{ctx}: key must start with {prefix}")
            check(tech == "both", f"{ctx}: x265 tech must be both")
            if scope == "settable":
                check(ENGINE_RE.match(engine) is not None, f"{ctx}: bad engine {engine!r}")
                check(engine not in X265_FORBIDDEN, f"{ctx}: forbidden engine {engine!r}")
                check(x265_engine_ok(engine, x265_anchors),
                      f"{ctx}: engine {engine!r} not in x265 anchors")
            else:
                check(engine == "", f"{ctx}: readonly row must not have engine")
        elif expected_backend == "libaom":
            prefix = "aom:" if scope == "settable" else "__aom:"
            check(key.startswith(prefix), f"{ctx}: key must start with {prefix}")
            check(tech == "both", f"{ctx}: libaom tech must be both")
            if scope == "settable":
                check(ENGINE_RE.match(engine) is not None, f"{ctx}: bad engine {engine!r}")
                check(engine not in AOM_FORBIDDEN, f"{ctx}: forbidden engine {engine!r}")
                check(not aom_anchors or engine in aom_anchors,
                      f"{ctx}: engine {engine!r} not in aom anchors")
            else:
                check(engine == "", f"{ctx}: readonly row must not have engine")
        elif expected_backend == "svt-av1":
            check(key.startswith("__svt:"), f"{ctx}: key must start with __svt:")
            check(scope == "readonly", f"{ctx}: svt catalog only readonly rows")
            check(engine == "", f"{ctx}: svt readonly row must not have engine")
            check(tech == "both", f"{ctx}: svt tech must be both")
        else:  # jxl
            check(JXL_KEYS_NO_PREFIX and ":" not in key, f"{ctx}: jxl key must not have prefix")
            check(engine == "", f"{ctx}: jxl row must not have engine")
            check(scope == "settable", f"{ctx}: jxl catalog only settable rows")

        # 形态 ↔ 类型 / scope 一致性
        if form == "readonly":
            check(scope == "readonly", f"{ctx}: form readonly requires scope readonly")
            check(typ == "enum", f"{ctx}: readonly row type must be enum")
        elif form == "text":
            check(typ == "text", f"{ctx}: form text requires type text")
            check(key in ("frame_name", "intrinsic_size"), f"{ctx}: only frame_name/intrinsic_size may be text")
            check(expected_format == "jxl", f"{ctx}: text only for jxl")
        elif form == "switch":
            check(typ == "bool", f"{ctx}: form switch requires type bool")
        elif form == "dropdown" or form == "mode":
            check(typ == "enum", f"{ctx}: form {form} requires type enum")
        elif form == "number":
            check(typ in ("int", "float"), f"{ctx}: form number requires int/float")

        # def / 域 / choices
        default = row.get("def", "")
        check(isinstance(default, str), f"{ctx}: def must be string")
        if typ in ("int", "float"):
            lo = row.get("lo")
            hi = row.get("hi")
            step = row.get("step", 1)
            check(isinstance(lo, (int, float)), f"{ctx}: missing lo")
            check(isinstance(hi, (int, float)), f"{ctx}: missing hi")
            check(float(lo) <= float(hi), f"{ctx}: lo > hi")
            check(float(step) > 0, f"{ctx}: step must be > 0")
            if default != "":
                try:
                    float(default)
                except ValueError:
                    raise SpecError(f"{ctx}: def {default!r} not numeric")
        elif typ == "bool":
            check(default in ("", "0", "1", "true", "false"), f"{ctx}: bad bool def {default!r}")
        elif typ == "enum":
            choices = row.get("choices")
            check(isinstance(choices, list) and choices, f"{ctx}: enum needs choices")
            names = []
            for choice in choices:
                check(isinstance(choice, list) and len(choice) == 2, f"{ctx}: bad choice {choice!r}")
                name, value = choice
                check(isinstance(name, str) and name != "", f"{ctx}: bad choice name")
                check(name not in names, f"{ctx}: duplicate choice {name!r}")
                names.append(name)
                check(isinstance(value, (str, int, float, bool)), f"{ctx}: bad choice value")
            check(default == "" or default in names, f"{ctx}: def {default!r} not a choice")
        elif typ == "text":
            check(default == "", f"{ctx}: text def must be empty")

        # 规则引用只做格式检查（实际规则表 B1 落地）
        if rule_ids:
            for rid in rule_ids.split(","):
                rid = rid.strip()
                check(re.fullmatch(r"[A-Z][A-Z0-9]+(?:-[A-Z0-9]+)*", rid) is not None,
                      f"{ctx}: bad rule id {rid!r}")
    return spec


def cpp_row(row: dict) -> str:
    typ = row["type"]
    choices = row.get("choices", [])
    choices_txt = "{}"
    if choices:
        items = []
        for name, value in choices:
            items.append(f'{{"{esc(name)}", {cpp_value(value)}}}')
        choices_txt = "{" + ", ".join(items) + "}"
    default = row.get("def", "")
    group_const = GROUPS[row["group"]]
    parts = [
        f'.key = "{esc(row["key"])}"',
        f'.engine = "{esc(row.get("engine", ""))}"',
        f'.label = "{esc(row["label"])}"',
        f'.type = {TYPES[typ]}',
        f'.lo = {float(row.get("lo", 0))!r}',
        f'.hi = {float(row.get("hi", 0))!r}',
        f'.step = {float(row.get("step", 1))!r}',
        f'.choices = {choices_txt}',
        f'.def = "{esc(default)}"',
        f'.form = {FORMS[row["form"]]}',
        f'.role = {ROLES[row["role"]]}',
        f'.scope = {SCOPES[row["scope"]]}',
        f'.tech = {TECHS[row.get("tech", "both")]}',
        f'.group = {group_const}',
        f'.rule_ids = "{esc(row.get("rules", ""))}"',
        f'.reason = "{esc(row["reason"])}"',
        f'.tooltip = "{esc(row.get("tooltip", ""))}"',
    ]
    return "PP_ROW{" + ", ".join(parts) + "},"


def render_inc(filename: str, format_id: str, backend: str, rows: list[dict]) -> str:
    lines = [
        "// SPDX-License-Identifier: GPL-3.0-or-later",
        f"// GENERATED by tools/gen_param_catalog.py — do not edit.",
        f"// source: tools/param_catalog_specs/{filename} | format={format_id} backend={backend}",
        f"// rows={len(rows)}",
        "",
    ]
    lines += [cpp_row(r) for r in rows]
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="do not write; fail if generated files stale")
    parser.add_argument("--validate-only", action="store_true",
                        help="validate specs only (teammates use this; does not read/write .inc)")
    parser.add_argument("--strict-counts", action="store_true", help="assert final row counts")
    parser.add_argument("--no-anchors", action="store_true", help="skip upstream anchor extraction")
    args = parser.parse_args()

    try:
        if args.no_anchors:
            x265_anchors: list[str] = []
            aom_anchors: list[str] = []
        else:
            x265_anchors = extract_x265()
            aom_anchors = extract_aom()
        x265_set, aom_set = set(x265_anchors), set(aom_anchors)

        seen: set[str] = set()
        renders: dict[str, tuple[str, str]] = {}
        for format_id, backend, filename, expected_count in BACKENDS:
            spec = validate_spec(load_json(SPEC_DIR / filename), filename, format_id, backend,
                                 x265_set, aom_set, seen)
            rows = spec["rows"]
            if args.strict_counts:
                check(len(rows) == expected_count,
                      f"{filename}: expected {expected_count} rows, got {len(rows)}")
            renders[f"{filename[:-5]}_catalog.inc"] = (
                render_inc(filename, format_id, backend, rows),
                f"{format_id}/{backend}",
            )

        anchors_inc = write_anchors(x265_anchors, aom_anchors)
        outputs = dict(renders)
        outputs["anchors.inc"] = (anchors_inc, "anchors")

        if args.validate_only:
            total = sum(len(json.loads((SPEC_DIR / b[2]).read_text("utf-8"))["rows"]) for b in BACKENDS)
            print(f"catalog specs OK: rows={total} x265_anchors={len(x265_anchors)} "
                  f"aom_anchors={len(aom_anchors)}")
            return 0

        if args.check:
            stale = []
            for name, (content, _label) in outputs.items():
                path = OUT_DIR / name
                if not path.is_file() or path.read_text(encoding="utf-8") != content:
                    stale.append(name)
            if stale:
                print("STALE: " + ", ".join(stale), file=sys.stderr)
                return 1
            print(f"catalog check OK: rows={sum(len(json.loads((SPEC_DIR / b[2]).read_text('utf-8'))['rows']) for b in BACKENDS)}"
                  f" x265_anchors={len(x265_anchors)} aom_anchors={len(aom_anchors)}")
            return 0

        OUT_DIR.mkdir(parents=True, exist_ok=True)
        for name, (content, _label) in outputs.items():
            (OUT_DIR / name).write_text(content, encoding="utf-8", newline="\n")
        total = sum(len(json.loads((SPEC_DIR / b[2]).read_text("utf-8"))["rows"]) for b in BACKENDS)
        print(f"catalog generated: rows={total} x265_anchors={len(x265_anchors)} aom_anchors={len(aom_anchors)}")
        return 0
    except SpecError as exc:
        print(f"catalog spec error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
