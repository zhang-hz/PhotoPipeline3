# `assets/icc/` — target ICC profiles

## Decision (M1 §3.6 ruling): generated in memory by lcms2, no binary ICC shipped

The Display P3 and Adobe RGB (1998) **target** profiles used by
`pp::ColorManager` are **generated in memory at runtime by lcms2**
(`cmsCreateRGBProfile`) and cached as serialized bytes. This directory
therefore deliberately contains **no** `DisplayP3.icc` / `AdobeRGB1998.icc`
file.

Rationale:

* **No redistribution question.** ICC profiles obtained from third parties
  (Adobe, HP/Microsoft, ICC registry) carry their own copyright/licence terms
  and would have to be documented as "source noted" redistributable assets.
  A profile built from published, purely numerical colourimetry (white point,
  primaries, transfer function) contains no third-party content, so nothing
  needs to be licensed or attributed.
* **Offline and byte-reproducible build.** No download step, no binary blob in
  the repository; the exact same profile bytes are produced on every machine
  and for every run.
* sRGB needs no file at all: the target uses lcms2's built-in profile
  (`cmsCreate_sRGBProfile()`), which is serialized to memory when it has to be
  embedded in an output file.

If an externally supplied ICC is ever required, it goes through the
"source noted" path (file header comment or this README) — see §3.6.

## Generation parameters (implemented in `src/core/colormanager.cpp`)

| Parameter | Display P3 | Adobe RGB (1998) |
|---|---|---|
| White point | D65 = `x 0.3127, y 0.3290, Y 1.0` | D65 = `x 0.3127, y 0.3290, Y 1.0` |
| Red primary | `x 0.680, y 0.320` | `x 0.640, y 0.330` |
| Green primary | `x 0.265, y 0.690` | `x 0.210, y 0.710` |
| Blue primary | `x 0.150, y 0.060` | `x 0.150, y 0.060` |
| TRC | IEC 61966-2.1 (sRGB) parametric curve, lcms2 type 4: `g 2.4, a 1/1.055, b 0.055/1.055, c 1/12.92, d 0.04045` | pure gamma `2.19921875` |
| Profile description tag | `Display P3` | `Adobe RGB (1998)` |
| Colour space / PCS | `RGB ` / `XYZ ` | `RGB ` / `XYZ ` |
| lcms2 calls | `cmsCreateRGBProfile` + `cmsWriteTag(cmsSigProfileDescriptionTag)` + `cmsSaveProfileToMem` | same |

The D65 white point and the sRGB transfer curve are the exact values lcms2
itself uses in `cmsCreateRGBProfile`-based profile construction
(`cmsvirt.c`). `cmsCreateRGBProfile` also writes the ICC V4
chromatic-adaptation tag (D65 → D50), so the profiles are valid V4 display
profiles with an XYZ PCS.

## How it is wired

* `pp::load_target_icc(ColorTarget, std::string& err)` — public frozen
  signature, unchanged. `SRGB` returns an empty string (lcms2 built-in),
  `KeepOriginal` returns an empty string (nothing to embed), `DisplayP3` /
  `AdobeRGB` return the generated-and-cached ICC bytes. It no longer reads
  this directory — the files named in the header comment
  (`DisplayP3.icc` / `AdobeRGB1998.icc`) do not exist by design.
* `pp::ColorManager::transform` builds the destination profile from those
  bytes with `cmsOpenProfileFromMem`, fixed intent
  `INTENT_RELATIVE_COLORIMETRIC | cmsFLAGS_BLACKPOINTCOMPENSATION`, and puts
  the target ICC into `ColorOutcome::icc_to_embed` so the encoder/metadata
  stage embeds it in the output.

## Self-check

`tests/unit/test_color.cpp` re-serializes both profiles
(`cmsSaveProfileToMem` → `cmsOpenProfileFromMem`), asserts
`cmsGetColorSpace == cmsSigRgbData`, the description tag, the primaries in
`cmsSigChromaticityTag` and the TRC value at 0.5 (EOTF(0.5) ≈ 0.21404 for
P3/sRGB-TRC, ≈ 0.21774 for Adobe RGB gamma 2.19921875).
