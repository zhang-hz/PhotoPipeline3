// SPDX-License-Identifier: BSD-3-Clause
// PhotoPipeline M8-C1 —— SSIMULACRA 2 兼容层核心（libjxl 内部依赖的最小子集）。
//
// 目的：为 vendored 的 libjxl v0.12.0 `tools/ssimulacra2.cc` + `tools/gauss_blur.cc`
// 提供**只读替身**：Status/StatusOr、ImageF/Image3F、ImageBundle、ColorEncoding、
// ThreadPool/RunOnPool、AlignedMemory、ImageMetadata。算法、权重、常量、尺度与
// 边界处理全部保留在 vendored 源码内（本文件不含任何评分公式）。
//
// 契约输入（src/core/metric/metric.h）：sRGB **线性** float RGB（行回调）。
// 因此 compat ToXYB 只实现 LinearSRGB→XYB 路径；sRGB gamma 输入经 ImageBundle::TransformTo
// 内精确 sRGB EOTF 线性化；其余 ColorEncoding 一律返回错误（不静默错算）。
//
// 与官方实现的差异（逐条记录于 third_party/ssimulacra2/README.md）：
//   * 容器：ImageF/Image3F 为连续 float 平面（行尾预留 16 float 以允许 Highway 满向量读）。
//   * 状态：Status/StatusOr 语义同官方（bool 化 + JXL_* 宏），实现裁剪。
//   * 调度：pool 恒为 nullptr 时 RunOnPool 串行执行（评分路径不建线程池）。
//   * 内存：全部分配走调用方提供的 JxlMemoryManager（NoMemoryManager 为 malloc/free）。
#ifndef PP_SSIMULACRA2_COMPAT_CORE_H_
#define PP_SSIMULACRA2_COMPAT_CORE_H_

#include <jxl/cms_interface.h>
#include <jxl/memory_manager.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace jxl {

// ===========================================================================
// lib/jxl/base/compiler_specific.h（宏子集）
// ===========================================================================
#if defined(_MSC_VER)
#define JXL_RESTRICT __restrict
#define JXL_INLINE inline
#else
#define JXL_RESTRICT __restrict__
#define JXL_INLINE inline
#endif

// ===========================================================================
// lib/jxl/base/common.h（子集）
// ===========================================================================
constexpr float kDefaultIntensityTarget = 255.0f;

constexpr inline size_t RoundUpTo(size_t what, size_t align) {
  return ((what + align - 1) / align) * align;
}

// ===========================================================================
// lib/jxl/base/status.h（Status / StatusOr / JXL_* 宏，语义同官方）
// ===========================================================================
enum class StatusCode : int32_t {
  kNotEnoughBytes = -1,
  kOk = 0,
  kGenericError = 1,
  kUnsupported = 2,
};

class Status {
 public:
  constexpr Status(bool ok)  // NOLINT(google-explicit-constructor)
      : code_(ok ? StatusCode::kOk : StatusCode::kGenericError) {}
  constexpr Status(StatusCode code)  // NOLINT(google-explicit-constructor)
      : code_(code) {}
  constexpr operator bool() const {  // NOLINT(google-explicit-constructor)
    return code_ == StatusCode::kOk;
  }
  constexpr StatusCode code() const { return code_; }

 private:
  StatusCode code_;
};

template <typename T>
class StatusOr {
  static_assert(std::is_move_constructible<T>::value &&
                    std::is_move_assignable<T>::value,
                "T must be move constructible and move assignable");

 public:
  StatusOr(StatusCode code)  // NOLINT(google-explicit-constructor)
      : code_(code) {}
  StatusOr(Status status)  // NOLINT(google-explicit-constructor)
      : code_(status.code()) {}
  template <typename U,
            typename = typename std::enable_if<
                std::is_constructible<T, U&&>::value>::type>
  StatusOr(U&& value)  // NOLINT(google-explicit-constructor)
      : code_(StatusCode::kOk) {
    new (Ptr()) T(std::forward<U>(value));
  }
  StatusOr(StatusOr&& other) noexcept {
    code_ = other.code_;
    if (other.ok()) new (Ptr()) T(std::move(*other.Ptr()));
  }
  StatusOr& operator=(StatusOr&& other) noexcept {
    if (this == &other) return *this;
    if (ok()) Ptr()->~T();
    code_ = other.code_;
    if (other.ok()) new (Ptr()) T(std::move(*other.Ptr()));
    return *this;
  }
  StatusOr(const StatusOr&) = delete;
  StatusOr& operator=(const StatusOr&) = delete;
  ~StatusOr() {
    if (ok()) Ptr()->~T();
  }

  bool ok() const { return code_ == StatusCode::kOk; }
  Status status() const { return code_; }

  // 只允许在 ok() 为真时调用（由 JXL_ASSIGN_OR_RETURN 使用）。
  T value_() && {
    return std::move(*Ptr());
  }

 private:
  T* Ptr() { return reinterpret_cast<T*>(&storage_); }
  const T* Ptr() const { return reinterpret_cast<const T*>(&storage_); }

  alignas(T) unsigned char storage_[sizeof(T)];
  StatusCode code_;
};

#define PP_COMPAT_JOIN_IMPL(a, b) a##b
#define PP_COMPAT_JOIN(a, b) PP_COMPAT_JOIN_IMPL(a, b)

#define JXL_FAILURE(format, ...) \
  ::jxl::Status(::jxl::StatusCode::kGenericError)

#define JXL_UNSUPPORTED(format, ...) \
  ::jxl::Status(::jxl::StatusCode::kUnsupported)

#define JXL_ENSURE(condition)                                  \
  do {                                                         \
    if (!(condition)) {                                        \
      return JXL_FAILURE("JXL_ENSURE: %s", #condition);        \
    }                                                          \
  } while (0)

#define JXL_DASSERT(condition) ((void)0)

#define JXL_UNREACHABLE(format, ...) \
  JXL_FAILURE("internal: " format, ##__VA_ARGS__)

#define JXL_RETURN_IF_ERROR(status)        \
  do {                                     \
    ::jxl::Status pp_compat_status = (status); \
    if (!pp_compat_status) return pp_compat_status; \
  } while (0)

#define JXL_ASSIGN_OR_RETURN(lhs, statusor)                          \
  auto PP_COMPAT_JOIN(pp_compat_assign_, __LINE__) = (statusor);     \
  JXL_RETURN_IF_ERROR(                                               \
      PP_COMPAT_JOIN(pp_compat_assign_, __LINE__).status());         \
  lhs = std::move(PP_COMPAT_JOIN(pp_compat_assign_, __LINE__)).value_()

// ===========================================================================
// lib/jxl/base/data_parallel.h（ThreadPool / RunOnPool，pool==nullptr 串行）
// ===========================================================================
struct ThreadPoolNoInit {};

class ThreadPool {
 public:
  static constexpr ThreadPoolNoInit NoInit{};
};

template <class DataFunc>
Status RunOnPool(ThreadPool* /*pool*/, const uint32_t begin,
                 const uint32_t end, const ThreadPoolNoInit& /*no_init*/,
                 const DataFunc& data_func, const char* caller) {
  for (uint32_t i = begin; i < end; ++i) {
    if (!data_func(i, static_cast<size_t>(0))) {
      return JXL_FAILURE("[%s] failed", caller);
    }
  }
  return true;
}

// ===========================================================================
// 内存分配（统一走 JxlMemoryManager；mm 为空时回退 malloc/free）
// ===========================================================================
inline void* CompatAlloc(JxlMemoryManager* mm, size_t size) {
  if (mm != nullptr && mm->alloc != nullptr) {
    return mm->alloc(mm->opaque, size);
  }
  return std::malloc(size);
}

inline void CompatFree(JxlMemoryManager* mm, void* address) {
  if (address == nullptr) return;
  if (mm != nullptr && mm->free != nullptr) {
    mm->free(mm->opaque, address);
    return;
  }
  std::free(address);
}

// ===========================================================================
// lib/jxl/memory_manager_internal.h（AlignedMemory，裁剪）
// ===========================================================================
class AlignedMemory {
 public:
  AlignedMemory() = default;
  AlignedMemory(const AlignedMemory&) = delete;
  AlignedMemory& operator=(const AlignedMemory&) = delete;
  AlignedMemory(AlignedMemory&& other) noexcept { MoveFrom(other); }
  AlignedMemory& operator=(AlignedMemory&& other) noexcept {
    if (this == &other) return *this;
    Reset();
    MoveFrom(other);
    return *this;
  }
  ~AlignedMemory() { Reset(); }

  static StatusOr<AlignedMemory> Create(JxlMemoryManager* memory_manager,
                                        size_t size, size_t pre_padding = 0) {
    AlignedMemory out;
    constexpr size_t kAlign = 64;
    constexpr size_t kMaxAlignSlack = kAlign - 1;
    if (size > static_cast<size_t>(-1) - pre_padding - kMaxAlignSlack) {
      return JXL_FAILURE("AlignedMemory::Create: size overflow");
    }
    const size_t bytes = size + pre_padding + kMaxAlignSlack;
    void* allocation = CompatAlloc(memory_manager, bytes);
    if (allocation == nullptr) {
      return JXL_FAILURE("AlignedMemory::Create: allocation failed");
    }
    uintptr_t address = reinterpret_cast<uintptr_t>(allocation) + pre_padding;
    address = (address + kMaxAlignSlack) & ~static_cast<uintptr_t>(kMaxAlignSlack);
    out.memory_manager_ = memory_manager;
    out.allocation_ = allocation;
    out.address_ = reinterpret_cast<void*>(address);
    return out;
  }

  explicit operator bool() const noexcept { return address_ != nullptr; }

  template <typename T>
  T* address() const {
    return reinterpret_cast<T*>(address_);
  }
  JxlMemoryManager* memory_manager() const { return memory_manager_; }

 private:
  void MoveFrom(AlignedMemory& other) {
    memory_manager_ = other.memory_manager_;
    allocation_ = other.allocation_;
    address_ = other.address_;
    other.memory_manager_ = nullptr;
    other.allocation_ = nullptr;
    other.address_ = nullptr;
  }
  void Reset() {
    if (allocation_ != nullptr) CompatFree(memory_manager_, allocation_);
    memory_manager_ = nullptr;
    allocation_ = nullptr;
    address_ = nullptr;
  }

  JxlMemoryManager* memory_manager_ = nullptr;
  void* allocation_ = nullptr;
  void* address_ = nullptr;
};

// ===========================================================================
// lib/jxl/image.h（ImageF / Image3F，连续 float 平面 + 行尾 pad）
// ===========================================================================
class ImageF {
 public:
  // Highway 满向量（AVX2=8 lane / AVX512=16 lane）读越 xsize 时仍在分配内。
  static constexpr size_t kLanePad = 16;

  ImageF() = default;
  ImageF(const ImageF& other) { CopyFrom(other); }
  ImageF& operator=(const ImageF& other) {
    if (this != &other) {
      Free();
      CopyFrom(other);
    }
    return *this;
  }
  ImageF(ImageF&& other) noexcept { MoveFrom(other); }
  ImageF& operator=(ImageF&& other) noexcept {
    if (this != &other) {
      Free();
      MoveFrom(other);
    }
    return *this;
  }
  ~ImageF() { Free(); }

  static StatusOr<ImageF> Create(JxlMemoryManager* memory_manager,
                                 size_t xsize, size_t ysize) {
    ImageF out;
    if (!out.Realloc(memory_manager, xsize, ysize)) {
      return JXL_FAILURE("ImageF::Create: allocation failed");
    }
    return out;
  }

  size_t xsize() const { return xsize_; }
  size_t ysize() const { return ysize_; }
  JxlMemoryManager* memory_manager() const { return memory_manager_; }

  float* Row(size_t y) { return data_ + y * stride_; }
  const float* ConstRow(size_t y) const { return data_ + y * stride_; }

  Status ShrinkTo(size_t xsize, size_t ysize) {
    if (xsize == xsize_ && ysize == ysize_) return true;
    if (!Realloc(memory_manager_, xsize, ysize)) {
      return JXL_FAILURE("ImageF::ShrinkTo: allocation failed");
    }
    return true;
  }

 private:
  bool Realloc(JxlMemoryManager* memory_manager, size_t xsize, size_t ysize) {
    Free();
    if (xsize == 0 || ysize == 0) return true;
    const size_t stride = RoundUpTo(xsize, kLanePad) + kLanePad;
    if (stride > static_cast<size_t>(-1) / sizeof(float) / ysize) {
      return false;
    }
    const size_t count = stride * ysize + kLanePad;
    float* data = static_cast<float*>(CompatAlloc(memory_manager,
                                                  count * sizeof(float)));
    if (data == nullptr) return false;
    memory_manager_ = memory_manager;
    xsize_ = xsize;
    ysize_ = ysize;
    stride_ = stride;
    data_ = data;
    return true;
  }
  void Free() {
    if (data_ != nullptr) CompatFree(memory_manager_, data_);
    memory_manager_ = nullptr;
    xsize_ = 0;
    ysize_ = 0;
    stride_ = 0;
    data_ = nullptr;
  }
  void MoveFrom(ImageF& other) {
    memory_manager_ = other.memory_manager_;
    xsize_ = other.xsize_;
    ysize_ = other.ysize_;
    stride_ = other.stride_;
    data_ = other.data_;
    other.memory_manager_ = nullptr;
    other.xsize_ = 0;
    other.ysize_ = 0;
    other.stride_ = 0;
    other.data_ = nullptr;
  }
  void CopyFrom(const ImageF& other) {
    if (!Realloc(other.memory_manager_, other.xsize_, other.ysize_)) return;
    if (data_ == nullptr) return;
    for (size_t y = 0; y < ysize_; ++y) {
      std::memcpy(Row(y), other.ConstRow(y), xsize_ * sizeof(float));
    }
  }

  JxlMemoryManager* memory_manager_ = nullptr;
  size_t xsize_ = 0;
  size_t ysize_ = 0;
  size_t stride_ = 0;
  float* data_ = nullptr;
};

class Image3F {
 public:
  Image3F() = default;

  static StatusOr<Image3F> Create(JxlMemoryManager* memory_manager,
                                  size_t xsize, size_t ysize) {
    Image3F out;
    for (size_t c = 0; c < 3; ++c) {
      JXL_ASSIGN_OR_RETURN(out.planes_[c],
                           ImageF::Create(memory_manager, xsize, ysize));
    }
    return out;
  }

  size_t xsize() const { return planes_[0].xsize(); }
  size_t ysize() const { return planes_[0].ysize(); }
  JxlMemoryManager* memory_manager() const {
    return planes_[0].memory_manager();
  }

  float* PlaneRow(size_t c, size_t y) { return planes_[c].Row(y); }
  const float* PlaneRow(size_t c, size_t y) const {
    return planes_[c].ConstRow(y);
  }
  float* Row(size_t y) { return PlaneRow(0, y); }
  const float* ConstRow(size_t y) const { return PlaneRow(0, y); }

  const ImageF& Plane(size_t c) const { return planes_[c]; }
  ImageF& Plane(size_t c) { return planes_[c]; }

  Status ShrinkTo(size_t xsize, size_t ysize) {
    for (size_t c = 0; c < 3; ++c) {
      JXL_RETURN_IF_ERROR(planes_[c].ShrinkTo(xsize, ysize));
    }
    return true;
  }

 private:
  ImageF planes_[3];
};

// ===========================================================================
// lib/jxl/image_ops.h（SameSize / CopyImageTo，裁剪）
// ===========================================================================
inline bool SameSize(const ImageF& a, const ImageF& b) {
  return a.xsize() == b.xsize() && a.ysize() == b.ysize();
}

inline Status CopyImageTo(const ImageF& from, ImageF* to) {
  JXL_ENSURE(to != nullptr);
  JXL_ENSURE(SameSize(from, *to));
  if (from.xsize() == 0 || from.ysize() == 0) return true;
  for (size_t y = 0; y < from.ysize(); ++y) {
    std::memcpy(to->Row(y), from.ConstRow(y), from.xsize() * sizeof(float));
  }
  return true;
}

inline Status CopyImageTo(const Image3F& from, Image3F* to) {
  JXL_ENSURE(to != nullptr);
  JXL_ENSURE(from.xsize() == to->xsize() && from.ysize() == to->ysize());
  for (size_t c = 0; c < 3; ++c) {
    JXL_RETURN_IF_ERROR(CopyImageTo(from.Plane(c), &to->Plane(c)));
  }
  return true;
}

// ===========================================================================
// lib/jxl/color_encoding_internal.h（ColorEncoding，裁剪：仅 sRGB 族）
// ===========================================================================
class ColorEncoding {
 public:
  static ColorEncoding LinearSRGB(bool is_gray = false) {
    return ColorEncoding(TransferFunction::kLinear, is_gray);
  }
  static ColorEncoding SRGB(bool is_gray = false) {
    return ColorEncoding(TransferFunction::kSRGB, is_gray);
  }

  bool IsGray() const { return is_gray_; }
  bool IsLinearSRGB() const {
    return transfer_function_ == TransferFunction::kLinear;
  }
  bool IsSRGB() const { return transfer_function_ == TransferFunction::kSRGB; }
  bool SameColorEncoding(const ColorEncoding& other) const {
    return transfer_function_ == other.transfer_function_ &&
           is_gray_ == other.is_gray_;
  }

 private:
  enum class TransferFunction { kLinear, kSRGB };
  ColorEncoding(TransferFunction transfer_function, bool is_gray)
      : transfer_function_(transfer_function), is_gray_(is_gray) {}

  TransferFunction transfer_function_ = TransferFunction::kLinear;
  bool is_gray_ = false;
};

// ===========================================================================
// lib/jxl/image_metadata.h（ImageMetadata，裁剪：强度目标）
// ===========================================================================
struct ImageMetadata {
  float intensity_target = kDefaultIntensityTarget;
  float IntensityTarget() const { return intensity_target; }
};

// ===========================================================================
// lib/jxl/image_bundle.h（ImageBundle，裁剪：RGB 三平面 + 线性 sRGB；无 alpha）
// ===========================================================================
class ImageBundle {
 public:
  explicit ImageBundle(JxlMemoryManager* memory_manager)
      : memory_manager_(memory_manager) {}
  ImageBundle(JxlMemoryManager* memory_manager, const ImageMetadata* metadata)
      : memory_manager_(memory_manager), metadata_(metadata) {}

  StatusOr<ImageBundle> Copy() const {
    ImageBundle copy(memory_manager_, metadata_);
    JXL_ASSIGN_OR_RETURN(
        copy.color_,
        Image3F::Create(memory_manager_, color_.xsize(), color_.ysize()));
    JXL_RETURN_IF_ERROR(CopyImageTo(color_, &copy.color_));
    copy.c_current_ = c_current_;
    return copy;
  }

  size_t xsize() const { return color_.xsize(); }
  size_t ysize() const { return color_.ysize(); }
  JxlMemoryManager* memory_manager() const { return memory_manager_; }

  bool HasColor() const { return color_.xsize() != 0; }
  const Image3F& color() const { return color_; }
  Image3F* color() { return &color_; }

  Status SetFromImage(Image3F&& color, const ColorEncoding& c_current) {
    color_ = std::move(color);
    c_current_ = c_current;
    return true;
  }

  const ColorEncoding& c_current() const { return c_current_; }
  bool IsGray() const { return c_current_.IsGray(); }
  bool IsSRGB() const { return c_current_.IsSRGB(); }
  bool IsLinearSRGB() const { return c_current_.IsLinearSRGB(); }

  // 契约路径（input 已是 sRGB 线性）为 no-op；sRGB gamma → 线性用精确分段公式。
  Status TransformTo(const ColorEncoding& c_desired,
                     const JxlCmsInterface& /*cms*/,
                     ThreadPool* /*pool*/ = nullptr) {
    if (c_desired.SameColorEncoding(c_current_)) return true;
    if (c_desired.IsLinearSRGB() && c_current_.IsSRGB()) {
      for (size_t y = 0; y < color_.ysize(); ++y) {
        for (size_t c = 0; c < 3; ++c) {
          float* row = color_.PlaneRow(c, y);
          for (size_t x = 0; x < color_.xsize(); ++x) {
            row[x] = LinearFromSRGB(row[x]);
          }
        }
      }
      c_current_ = c_desired;
      return true;
    }
    return JXL_FAILURE("compat ImageBundle::TransformTo: unsupported encoding");
  }

  bool HasAlpha() const { return false; }
  const ImageF* alpha() const { return nullptr; }
  ImageF* alpha() { return nullptr; }
  const ImageF* black() const { return nullptr; }
  void ClearExtraChannels() {}

  const ImageMetadata* metadata() const { return metadata_; }

 private:
  static float LinearFromSRGB(float encoded) {
    // 标准 sRGB EOTF（分段）；官方工具此步走 lcms2，契约路径不经过这里。
    if (encoded <= 0.04045f) return encoded / 12.92f;
    return static_cast<float>(
        std::pow((static_cast<double>(encoded) + 0.055) / 1.055, 2.4));
  }

  JxlMemoryManager* memory_manager_ = nullptr;
  const ImageMetadata* metadata_ = nullptr;
  Image3F color_;
  ColorEncoding c_current_ = ColorEncoding::LinearSRGB(false);
};

}  // namespace jxl

#endif  // PP_SSIMULACRA2_COMPAT_CORE_H_
