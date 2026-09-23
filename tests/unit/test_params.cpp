// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — unit tests: parameter engine (M1-T2)
// 手写断言；失败打印 "FAIL <case>: <detail>"，main 返回失败数。
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "codecs/encoders.h"
#include "core/params.h"

namespace {

int g_fail = 0;

void fail(const std::string &c, const std::string &d) {
    std::printf("FAIL %s: %s\n", c.c_str(), d.c_str());
    ++g_fail;
}

void check(bool ok, const std::string &c, const std::string &d) {
    if (!ok)
        fail(c, d);
}

bool has(const pp::ParamSet &s, const char *key) { return s.count(key) != 0; }

bool contains(const std::vector<std::string> &v, const std::string &s) {
    for (const std::string &x : v)
        if (x == s)
            return true;
    return false;
}

std::string join(const std::vector<std::string> &v) {
    std::string out;
    for (const std::string &s : v)
        out += (out.empty() ? "" : ",") + s;
    return out;
}

std::string repr(const pp::ParamValue &v) {
    if (std::holds_alternative<bool>(v))
        return std::get<bool>(v) ? "bool(true)" : "bool(false)";
    if (std::holds_alternative<int64_t>(v))
        return "int(" + std::to_string(std::get<int64_t>(v)) + ")";
    if (std::holds_alternative<double>(v))
        return "float(" + std::to_string(std::get<double>(v)) + ")";
    if (std::holds_alternative<std::string>(v))
        return "str(" + std::get<std::string>(v) + ")";
    return "empty";
}

const pp::ParamDef *param_of(const pp::TechDef &t, const char *key) {
    for (const pp::ParamDef &p : t.params)
        if (p.key == key)
            return &p;
    return nullptr;
}

struct Slot {
    const pp::FormatDef *f = nullptr;
    const pp::BackendDef *b = nullptr;
    const pp::TechDef *t = nullptr;
};

Slot slot(const char *fmt, const char *backend, const char *tech) {
    Slot s;
    s.f = pp::find_format(fmt);
    if (s.f)
        s.b = pp::find_backend(*s.f, backend);
    if (s.b)
        s.t = pp::find_tech(*s.b, tech);
    return s;
}

} // namespace

int main() {
    using namespace pp;

    // 1) 默认值全集与表一致（并逐一通过 validate_params）
    {
        const std::string c = "defaults-match-table";
        std::size_t techs = 0;
        for (const FormatDef &f : static_formats()) {
            for (const BackendDef &b : f.backends) {
                for (const TechDef &t : b.techs) {
                    ++techs;
                    const std::string where = f.id + "/" + b.id + "/" + t.id;
                    const ParamSet s = default_params(f, b.id, t.id, t.lossless_capable);
                    for (const ParamDef &p : t.params) {
                        if (!has(s, p.key.c_str())) {
                            fail(c, where + ": missing key " + p.key);
                            continue;
                        }
                        if (!(s.at(p.key) == p.def))
                            fail(c, where + ": " + p.key + " = " + repr(s.at(p.key)) + " != def " +
                                        repr(p.def));
                    }
                    if (s.size() != t.params.size() + 1)
                        fail(c, where + ": size " + std::to_string(s.size()) + " != params+1 (" +
                                    std::to_string(t.params.size() + 1) + ")");
                    if (param_bool(s, "__lossless", !t.lossless_capable) != t.lossless_capable)
                        fail(c, where + ": __lossless flag not set to " +
                                    (t.lossless_capable ? "true" : "false"));
                    const std::string err = validate_params(f, b.id, t.id, t.lossless_capable, s);
                    if (!err.empty())
                        fail("validate-defaults", where + ": " + err);
                }
            }
        }
        if (techs != 8)
            fail(c, "expected 8 static techs, got " + std::to_string(techs));
    }

    // 2) 技术默认选择：空 tech_id + lossless → lossless_capable 技术
    {
        const std::string c = "defaults-pick-tech";
        const Slot jxl = slot("jxl", "libjxl", "modular");
        const ParamSet l = default_params(*jxl.f, "libjxl", "", true);
        check(has(l, "modular_predictor") && !has(l, "epf"), c, "jxl lossless should pick modular");
        check(param_float(l, "distance", -1) == 0.0, c, "modular distance default should be 0");
        const ParamSet q = default_params(*jxl.f, "libjxl", "", false);
        check(has(q, "epf") && !has(q, "modular_predictor"), c, "jxl lossy should pick vardct");
        const Slot webp = slot("webp", "libwebp", "lossless");
        const ParamSet w = default_params(*webp.f, "libwebp", "", true);
        check(has(w, "exact") && !has(w, "sharp_yuv"), c,
              "webp lossless should pick lossless tech");
        const ParamSet wl = default_params(*webp.f, "libwebp", "", false);
        check(has(wl, "sharp_yuv") && !has(wl, "exact"), c, "webp lossy should pick lossy tech");
    }

    // 3) JPEG 可见性：quality_mode 二选一
    {
        const std::string c = "visible-quality-mode";
        const Slot j = slot("jpeg", "jpegli", "dct");
        const ParamDef *dist = param_of(*j.t, "distance");
        const ParamDef *qual = param_of(*j.t, "quality");
        check(dist && qual, c, "distance/quality ParamDef not found");
        if (dist && qual) {
            ParamSet s = default_params(*j.f, "jpegli", "dct", false);
            check(eval_visible(*dist, s) && !eval_visible(*qual, s), c,
                  "default (distance) → distance visible, quality hidden");
            s["quality_mode"] = std::string("quality");
            check(!eval_visible(*dist, s) && eval_visible(*qual, s), c,
                  "quality mode → quality visible, distance hidden");
            s.erase("quality_mode");
            check(eval_visible(*dist, s) && !eval_visible(*qual, s), c,
                  "missing quality_mode falls back to distance");
        }
        // 无谓词参数恒可见、恒不锁定
        const ParamDef *chroma = param_of(*j.t, "chroma");
        check(chroma && eval_visible(*chroma, ParamSet{}) && !eval_lock(*chroma, ParamSet{}), c,
              "chroma should be always visible and never locked");
    }

    // 4) JPEG progressive>0 ⇒ optimize_coding 锁定 true（apply_locks 改写）
    {
        const std::string c = "lock-progressive";
        const Slot j = slot("jpeg", "jpegli", "dct");
        const ParamDef *opt = param_of(*j.t, "optimize_coding");
        check(opt != nullptr, c, "optimize_coding not found");
        if (opt) {
            ParamSet s = default_params(*j.f, "jpegli", "dct", false);
            const std::optional<ParamValue> lv = eval_lock(*opt, s);
            check(lv && std::holds_alternative<bool>(*lv) && std::get<bool>(*lv), c,
                  "progressive=true should lock optimize_coding=true");
            s["progressive"] = false;
            check(!eval_lock(*opt, s).has_value(), c, "progressive=false should not lock");
            s["progressive"] = true;
            s["optimize_coding"] = false;
            const std::vector<std::string> changed = apply_locks(*j.f, "jpegli", "dct", false, s);
            check(contains(changed, "optimize_coding"), c,
                  "apply_locks should report optimize_coding, got [" + join(changed) + "]");
            check(param_bool(s, "optimize_coding", false), c,
                  "optimize_coding should be rewritten true");
        }
    }

    // 5) JXL lossless：modular distance→0 / modular_lossy_palette→false；vardct 组不可见
    {
        const std::string c = "locks-jxl-lossless";
        const Slot jxl = slot("jxl", "libjxl", "modular");
        ParamSet s = default_params(*jxl.f, "libjxl", "modular", false);
        s["distance"] = 5.0;
        s["modular_lossy_palette"] = true;
        const std::vector<std::string> changed = apply_locks(*jxl.f, "libjxl", "modular", true, s);
        check(contains(changed, "distance") && contains(changed, "modular_lossy_palette"), c,
              "changed=[" + join(changed) + "]");
        check(param_float(s, "distance", -1) == 0.0, c,
              "distance = " + std::to_string(param_float(s, "distance", -1)));
        check(!param_bool(s, "modular_lossy_palette", true), c,
              "modular_lossy_palette should be false");
        const std::vector<std::string> again = apply_locks(*jxl.f, "libjxl", "modular", true, s);
        check(again.empty(), c, "second apply_locks should be a no-op, got [" + join(again) + "]");

        const ParamSet locked = default_params(*jxl.f, "libjxl", "modular", true);
        check(has(locked, "__lossless"), c, "default_params must carry the __lossless key");
        const ParamDef *md = param_of(*jxl.t, "distance");
        const ParamDef *mp = param_of(*jxl.t, "modular_lossy_palette");
        const std::optional<ParamValue> dl = md ? eval_lock(*md, locked) : std::nullopt;
        check(dl && std::holds_alternative<double>(*dl) && std::get<double>(*dl) == 0.0, c,
              "eval_lock(modular distance, lossless) should be 0.0");
        const std::optional<ParamValue> pl = mp ? eval_lock(*mp, locked) : std::nullopt;
        check(pl && std::holds_alternative<bool>(*pl) && !std::get<bool>(*pl), c,
              "eval_lock(modular_lossy_palette, lossless) should be false");
        check(!eval_lock(*md, default_params(*jxl.f, "libjxl", "modular", false)).has_value(), c,
              "lossy modular distance must not be locked");

        const Slot vd = slot("jxl", "libjxl", "vardct");
        const ParamDef *epf = param_of(*vd.t, "epf");
        const ParamDef *photon = param_of(*vd.t, "photon_noise");
        const ParamSet vl = default_params(*vd.f, "libjxl", "vardct", true);
        const ParamSet vq = default_params(*vd.f, "libjxl", "vardct", false);
        check(epf && photon, c, "epf/photon_noise not found");
        if (epf && photon) {
            check(!eval_visible(*epf, vl) && !eval_visible(*photon, vl), c,
                  "lossless → vardct group hidden (epf/photon_noise)");
            check(eval_visible(*epf, vq) && eval_visible(*photon, vq), c,
                  "lossy → vardct epf/photon_noise visible");
        }
    }

    // 6) WebP lossless：exact 可见（无损档）
    {
        const std::string c = "visible-webp-exact";
        const Slot w = slot("webp", "libwebp", "lossless");
        const ParamDef *exact = param_of(*w.t, "exact");
        check(exact != nullptr, c, "exact not found");
        if (exact) {
            check(eval_visible(*exact, default_params(*w.f, "libwebp", "lossless", true)), c,
                  "lossless → exact visible");
            check(!eval_visible(*exact, default_params(*w.f, "libwebp", "lossless", false)), c,
                  "lossy flag → exact hidden");
        }
    }

    // 7) TIFF 可见性：deflate_level ⇔ zip；predictor ⇔ lzw|zip
    {
        const std::string c = "visible-tiff-compression";
        const Slot t = slot("tiff", "oiio", "codec");
        const ParamDef *dl = param_of(*t.t, "deflate_level");
        const ParamDef *pr = param_of(*t.t, "predictor");
        check(dl && pr, c, "deflate_level/predictor not found");
        if (dl && pr) {
            ParamSet s = default_params(*t.f, "oiio", "codec", true);
            check(!eval_visible(*dl, s) && eval_visible(*pr, s), c, "lzw: predictor only");
            s["compression"] = std::string("zip");
            check(eval_visible(*dl, s) && eval_visible(*pr, s), c, "zip: both visible");
            s["compression"] = std::string("none");
            check(!eval_visible(*dl, s) && !eval_visible(*pr, s), c, "none: both hidden");
            s.erase("compression");
            check(!eval_visible(*dl, s) && eval_visible(*pr, s), c,
                  "missing compression → lzw default");
        }
    }

    // 8) validate_params：越界 / 非法枚举 / 类型不符 / TIFF tile
    {
        const Slot e = slot("jxl", "libjxl", "vardct");
        ParamSet s = default_params(*e.f, "libjxl", "vardct", false);
        s["effort"] = int64_t(11);
        std::string err = validate_params(*e.f, "libjxl", "vardct", false, s);
        check(!err.empty() && err.find("effort") != std::string::npos, "validate-range",
              "[" + err + "]");
        s["effort"] = int64_t(1);
        s["distance"] = 99.0;
        err = validate_params(*e.f, "libjxl", "vardct", false, s);
        check(!err.empty() && err.find("distance") != std::string::npos, "validate-range",
              "[" + err + "]");

        const Slot j = slot("jpeg", "jpegli", "dct");
        ParamSet js = default_params(*j.f, "jpegli", "dct", false);
        js["quality_mode"] = std::string("bogus");
        err = validate_params(*j.f, "jpegli", "dct", false, js);
        check(!err.empty() && err.find("quality_mode") != std::string::npos, "validate-enum",
              "[" + err + "]");
        js["quality_mode"] = std::string("distance");
        js["chroma"] = int64_t(444);
        err = validate_params(*j.f, "jpegli", "dct", false, js);
        check(!err.empty() && err.find("chroma") != std::string::npos, "validate-type",
              "[" + err + "]");
        js["chroma"] = std::string("444");
        js["quality"] = std::string("90");
        err = validate_params(*j.f, "jpegli", "dct", false, js);
        check(!err.empty() && err.find("quality") != std::string::npos, "validate-type",
              "[" + err + "]");
        js["quality"] = int64_t(90);
        err = validate_params(*j.f, "jpegli", "dct", false, js);
        check(err.empty(), "validate-type", "clean set should pass, got [" + err + "]");
        // 保留键（"__" 前缀）不参与范围/类型校验（§3.4 保留键约定）
        js["__lossless"] = std::string("not a bool");
        js["__anything"] = int64_t(123);
        err = validate_params(*j.f, "jpegli", "dct", false, js);
        check(err.empty(), "validate-reserved-key", "[" + err + "]");
        js.erase("__lossless");
        js.erase("__anything");
        js["optimize_coding"] = std::string("yes");
        err = validate_params(*j.f, "jpegli", "dct", false, js);
        check(!err.empty() && err.find("optimize_coding") != std::string::npos, "validate-type",
              "[" + err + "]");
    }
    {
        const std::string c = "validate-tiff-tile";
        const Slot t = slot("tiff", "oiio", "codec");
        ParamSet s = default_params(*t.f, "oiio", "codec", true);
        std::string err = validate_params(*t.f, "oiio", "codec", true, s);
        check(err.empty(), c, "strips (0/0) should pass, got [" + err + "]");
        s["tiff_tile_width"] = int64_t(128);
        s["tiff_tile_height"] = int64_t(128);
        err = validate_params(*t.f, "oiio", "codec", true, s);
        check(err.empty(), c, "128x128 should pass, got [" + err + "]");
        s["tiff_tile_width"] = int64_t(100);
        err = validate_params(*t.f, "oiio", "codec", true, s);
        check(!err.empty() && err.find("tiff_tile_width") != std::string::npos, c, "[" + err + "]");
        s["tiff_tile_width"] = int64_t(128);
        s["tiff_tile_height"] = int64_t(0);
        err = validate_params(*t.f, "oiio", "codec", true, s);
        check(!err.empty() && err.find("tiff_tile") != std::string::npos, c, "[" + err + "]");
        s["tiff_tile_width"] = int64_t(0);
        s["tiff_tile_height"] = int64_t(96);
        err = validate_params(*t.f, "oiio", "codec", true, s);
        check(!err.empty() && err.find("tiff_tile_height") != std::string::npos, c,
              "[" + err + "]");
        // 未知后端 / 未知技术
        err = validate_params(*t.f, "bogus", "codec", true, s);
        check(!err.empty() && err.find("bogus") != std::string::npos, c, "[" + err + "]");
        err = validate_params(*t.f, "oiio", "bogus", true, s);
        check(!err.empty() && err.find("bogus") != std::string::npos, c, "[" + err + "]");
        // heif/avif 运行时内省：静态表跳过参数校验
        const Slot h = slot("heif", "x265", "");
        err = validate_params(*h.f, "x265", "anything", false, ParamSet{});
        check(err.empty(), c, "runtime-introspected backend should be skipped, got [" + err + "]");
    }

    // 9) param_int/float/bool/str：取值与 fallback
    {
        const std::string c = "param-accessors";
        ParamSet s;
        s["i"] = int64_t(7);
        s["f"] = 2.5;
        s["b"] = true;
        s["t"] = std::string("x");
        check(param_int(s, "i", -1) == 7, c, "int hit");
        check(param_int(s, "f", -1) == -1, c, "double must not satisfy param_int");
        check(param_int(s, "missing", -1) == -1, c, "int miss → fallback");
        check(param_float(s, "f", -1) == 2.5, c, "float hit");
        check(param_float(s, "i", -1) == 7.0, c, "int widens to float");
        check(param_float(s, "t", -1) == -1, c, "string must not satisfy param_float");
        check(param_float(s, "missing", -1) == -1, c, "float miss → fallback");
        check(param_bool(s, "b", false), c, "bool hit");
        check(!param_bool(s, "t", false), c, "string must not satisfy param_bool");
        check(param_bool(s, "missing", true), c, "bool miss → fallback");
        check(param_str(s, "t", "d") == "x", c, "string hit");
        check(param_str(s, "i", "d") == "d", c, "int must not satisfy param_str");
        check(param_str(s, "missing", "d") == "d", c, "string miss → fallback");
    }

    // 10) find_format / find_backend / find_tech：命中与未命中
    {
        const std::string c = "find-functions";
        const FormatDef *jxl = find_format("jxl");
        check(jxl && jxl->id == "jxl", c, "find_format(jxl)");
        check(find_format("nope") == nullptr, c, "find_format(nope) should be null");
        check(find_format("") == nullptr, c, "find_format(\"\") should be null");
        if (jxl) {
            const BackendDef *b = find_backend(*jxl, "libjxl");
            check(b && b->id == "libjxl", c, "find_backend(libjxl)");
            check(find_backend(*jxl, "") == &jxl->backends.front(), c,
                  "empty backend → first (首选)");
            check(find_backend(*jxl, "nope") == nullptr, c, "find_backend(nope) should be null");
            if (b) {
                const TechDef *t = find_tech(*b, "modular");
                check(t && t->id == "modular", c, "find_tech(modular)");
                check(find_tech(*b, "") == &b->techs.front(), c, "empty tech → first");
                check(find_tech(*b, "nope") == nullptr, c, "find_tech(nope) should be null");
            }
        }
    }

    // 11) fill_defaults：补齐缺失 key（保留已有值），第二次为空
    {
        const std::string c = "fill-defaults";
        const Slot jxl = slot("jxl", "libjxl", "modular");
        ParamSet s;
        s["effort"] = int64_t(3);
        const std::vector<std::string> added = fill_defaults(*jxl.f, "libjxl", "modular", false, s);
        check(!contains(added, "effort"), c, "existing key must not be reported");
        check(contains(added, "distance") && contains(added, "modular_predictor") &&
                  contains(added, "color_transform"),
              c, "missing keys reported: [" + join(added) + "]");
        check(param_int(s, "effort", 0) == 3, c, "existing value preserved");
        check(s.size() == jxl.t->params.size(), c,
              "size " + std::to_string(s.size()) + " != " + std::to_string(jxl.t->params.size()));
        check(fill_defaults(*jxl.f, "libjxl", "modular", false, s).empty(), c,
              "second fill_defaults should add nothing");
        check(!has(s, "__lossless"), c, "fill_defaults must not inject reserved keys");
    }

    // 12) snapshot_params：按 key 字典序、稳定、值字符串化
    {
        const std::string c = "snapshot-order";
        ParamSet s;
        s["zeta"] = int64_t(1);
        s["alpha"] = true;
        s["mid"] = 1.5;
        s["name"] = std::string("x y");
        s["empty"] = ParamValue{};
        const std::string snap = snapshot_params(s);
        check(snap == "alpha=true empty= mid=1.5 name=x y zeta=1", c, "[" + snap + "]");
        check(snapshot_params(s) == snap, c, "snapshot must be stable across calls");
        // 保留键（"__" 前缀）不进快照（§3.4 保留键约定）
        const Slot jxl = slot("jxl", "libjxl", "vardct");
        const ParamSet def = default_params(*jxl.f, "libjxl", "vardct", false);
        check(has(def, "__lossless"), c, "default_params must still carry __lossless");
        const std::string def_snap = snapshot_params(def);
        check(def_snap.find("__lossless") == std::string::npos, c,
              "reserved keys must be excluded from the snapshot; got [" + def_snap.substr(0, 60) +
                  "...]");
        ParamSet mixed;
        mixed["__lossless"] = true;
        mixed["effort"] = int64_t(7);
        check(snapshot_params(mixed) == "effort=7", c, "[" + snapshot_params(mixed) + "]");
    }

    // 13) 静态位深允许集（M1-T2b：heif/avif = {8,10,12}；表只表达"允许集"，默认位深由调用方选）
    {
        const std::string c = "bitdepth-sets";
        for (const char *id : {"heif", "avif"}) {
            const FormatDef *f = find_format(id);
            check(f != nullptr, c, std::string(id) + " not found");
            if (f) {
                for (const int d : {8, 10, 12})
                    check(std::find(f->bitdepths.begin(), f->bitdepths.end(), d) !=
                              f->bitdepths.end(),
                          c, std::string(id) + " must allow bitdepth " + std::to_string(d));
                check(f->bitdepths.size() == 3, c,
                      std::string(id) +
                          " bitdepth set size = " + std::to_string(f->bitdepths.size()));
            }
        }
        const FormatDef *jpeg = find_format("jpeg");
        check(jpeg && std::find(jpeg->bitdepths.begin(), jpeg->bitdepths.end(), 12) ==
                          jpeg->bitdepths.end(),
              c, "jpeg must not allow 12-bit output");
        const FormatDef *png = find_format("png");
        check(png && std::find(png->bitdepths.begin(), png->bitdepths.end(), 10) ==
                         png->bitdepths.end(),
              c, "png must not allow 10-bit output");
    }

    // 14) cross_validate 规则①（M2-T5 §2.7）：webp + lossy 的 qmin ≤ qmax
    {
        const std::string c = "cross-webp-qmin-qmax";
        const Slot w = slot("webp", "libwebp", "lossy");
        ParamSet s = default_params(*w.f, "libwebp", "lossy", false);
        s["qmin"] = int64_t(90);
        s["qmax"] = int64_t(100);
        std::vector<std::string> m = cross_validate(s, "webp", "lossy");
        check(m.empty(), c, "qmin<qmax 不应报，[" + join(m) + "]");
        s["qmin"] = int64_t(100);
        check(cross_validate(s, "webp", "lossy").empty(), c, "qmin==qmax 不算违规");
        s["qmin"] = int64_t(100);
        s["qmax"] = int64_t(50);
        m = cross_validate(s, "webp", "lossy");
        check(m.size() == 1 && m[0] == "qmin 不能大于 qmax", c, "正例失败，[" + join(m) + "]");
        // tech 传递：tech_id 空 = 首选技术（webp 首选 = lossy）→ 规则照样生效
        m = cross_validate(s, "webp", "");
        check(m.size() == 1 && m[0] == "qmin 不能大于 qmax", c,
              "空 tech 应解析为首选 lossy，[" + join(m) + "]");
        // lossless 技术不适用（§2.7 冻结为 "webp 且 lossy"）
        m = cross_validate(s, "webp", "lossless");
        check(m.empty(), c, "lossless 不应报 qmin/qmax，[" + join(m) + "]");
        // 缺键 → 无法判定（缺失=默认值，归 fill_defaults/validate_params）
        ParamSet partial = default_params(*w.f, "libwebp", "lossy", false);
        partial.erase("qmax");
        partial["qmin"] = int64_t(100);
        check(cross_validate(partial, "webp", "lossy").empty(), c, "缺 qmax 不应报");
    }

    // 15) cross_validate 规则②（M2-T5 §2.7 父裁定订正）：jpeg progressive ⇒ optimize_coding
    {
        const std::string c = "cross-jpeg-progressive";
        const Slot j = slot("jpeg", "jpegli", "dct");
        const ParamSet def = default_params(*j.f, "jpegli", "dct", false);
        const bool def_both_true =
            param_bool(def, "progressive", false) && param_bool(def, "optimize_coding", false);
        check(def_both_true, c,
              "前提失败：jpeg 默认值应为 (progressive=true, optimize_coding=true)");
        // 关键防误报回归：默认态经 cross_validate 必须为空（§4 T5）
        std::vector<std::string> m = cross_validate(def, "jpeg", "dct");
        check(m.empty(), c, "jpeg 默认态不得报，[" + join(m) + "]");
        // 正例：(true,false)
        ParamSet s = def;
        s["optimize_coding"] = false;
        m = cross_validate(s, "jpeg", "dct");
        check(m.size() == 1 && m[0] == "启用渐进式时必须启用哈夫曼表优化", c,
              "正例失败，[" + join(m) + "]");
        // 负例：(false,false) / (false,true) / 缺键
        s["progressive"] = false;
        check(cross_validate(s, "jpeg", "dct").empty(), c, "(false,false) 不应报");
        s["optimize_coding"] = true;
        check(cross_validate(s, "jpeg", "dct").empty(), c, "(false,true) 不应报");
        ParamSet missing = def;
        missing.erase("progressive");
        check(cross_validate(missing, "jpeg", "dct").empty(), c, "缺 progressive 不应报");
        ParamSet missing2 = def;
        missing2["optimize_coding"] = false;
        missing2.erase("progressive");
        check(cross_validate(missing2, "jpeg", "dct").empty(), c,
              "缺 progressive 不应报（即使 optimize=false）");
        // 空集（pipeline 契约测试的既有调用形态）不得报
        check(cross_validate(ParamSet{}, "jpeg", "").empty(), c, "空参数集不应报");
    }

    // 16) cross_validate 规则③（M2-T5 父裁定新增）：未知参数 = 该 format 全部技术键并集之外
    {
        const std::string c = "cross-unknown-params";
        // 跨技术防误报①：jxl modular 的工作集里塞入"仅 vardct 声明"的键 → 必须为空
        const Slot jm = slot("jxl", "libjxl", "modular");
        const Slot jv = slot("jxl", "libjxl", "vardct");
        check(param_of(*jm.t, "epf") == nullptr && param_of(*jv.t, "epf") != nullptr, c,
              "前提失败：epf 应只由 vardct 声明");
        ParamSet cross = default_params(*jm.f, "libjxl", "modular", false);
        cross["epf"] = int64_t(3); // 人为塞入的"仅另一技术声明"的键
        cross["photon_noise"] = 0.01;
        std::vector<std::string> m = cross_validate(cross, "jxl", "modular");
        check(m.empty(), c, "跨技术键不得误报，[" + join(m) + "]");
        // 跨技术防误报②：webp lossy 的工作集里塞入"仅 lossless 声明"的 exact
        const Slot wl = slot("webp", "libwebp", "lossy");
        check(param_of(*wl.t, "exact") == nullptr, c, "前提失败：lossy 不应声明 exact");
        ParamSet cross2 = default_params(*wl.f, "libwebp", "lossy", false);
        cross2["exact"] = true;
        check(cross_validate(cross2, "webp", "lossy").empty(), c,
              "同 format 其它技术的键不得误报，[" + join(cross_validate(cross2, "webp", "lossy")) +
                  "]");
        // 真未知键：单条
        ParamSet u = default_params(*slot("jpeg", "jpegli", "dct").f, "jpegli", "dct", false);
        u["bogus"] = std::string("1");
        m = cross_validate(u, "jpeg", "dct");
        check(m.size() == 1 && m[0] == "未知参数：bogus", c, "[" + join(m) + "]");
        // 多条 → 按字典序稳定（ParamSet=std::map）
        u["zzz"] = int64_t(1);
        u["aaa"] = std::string("x");
        u["mmm"] = true;
        m = cross_validate(u, "jpeg", "dct");
        const bool sorted = m.size() == 4 && m[0] == "未知参数：aaa" && m[1] == "未知参数：bogus" &&
                            m[2] == "未知参数：mmm" && m[3] == "未知参数：zzz";
        check(sorted, c, "字典序/条数不符，[" + join(m) + "]");
        check(cross_validate(u, "jpeg", "dct") == m, c, "同一集合重复调用应稳定");
        // 保留键（"__" 前缀）不参与判定
        u["__lossless"] = true;
        u["__anything"] = int64_t(1);
        check(cross_validate(u, "jpeg", "dct") == m, c,
              "保留键不得判为未知，[" + join(cross_validate(u, "jpeg", "dct")) + "]");
        // 默认集（无残留）→ 空
        check(cross_validate(
                  default_params(*slot("png", "oiio", "deflate").f, "oiio", "deflate", true), "png",
                  "deflate")
                  .empty(),
              c, "默认集不应报");
        // 未知 format：无法判定 → 不报（格式名校验归 validate_params）
        check(cross_validate(u, "nope", "x").empty(), c, "未知 format 不应判未知参数");
        // 未知 backend/tech 但 format 合法：仍按该 format 的并集判定
        check(cross_validate(cross, "jxl", "bogus-tech").empty(), c, "未知 tech 不改变键并集判定");
    }

    // 17) cross_validate 规则③ + libheif 运行时内省（heif/avif 静态表无技术）
    {
        const std::string c = "cross-heif-introspection";
        const std::vector<BackendDef> live = introspect_backends("heif");
        const bool have_live = !live.empty() && !live.front().techs.empty() &&
                               !live.front().techs.front().params.empty();
        if (!have_live) {
            std::printf("test_params: NOTE heif introspection unavailable — "
                        "cross_validate heif case skipped\n");
        } else {
            ParamSet s;
            for (const ParamDef &p : live.front().techs.front().params)
                s[p.key] = p.def;
            std::vector<std::string> m = cross_validate(s, "heif", "runtime");
            check(m.empty(), c, "内省声明的键不得判未知，[" + join(m) + "]");
            s["zz_bogus_key"] = int64_t(1);
            m = cross_validate(s, "heif", "runtime");
            check(m.size() == 1 && m[0] == "未知参数：zz_bogus_key", c, "[" + join(m) + "]");
        }
    }

    if (g_fail == 0)
        std::printf("test_params: OK\n");
    else
        std::printf("test_params: %d failure(s)\n", g_fail);
    return g_fail;
}
