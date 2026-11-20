# Frontier C++ Coding Standards

This document establishes the C++ language baseline, modular template architecture, function-prototyping
discipline, memory invariants, and cross-platform portability rules for **Frontier**.

Visual layout, file banners, Allman braces, and unit-tagged alignment are governed by
[AgenticInstructions/SKILL-Formatting.md](AgenticInstructions/SKILL-Formatting.md). Identifier construction
and banned words are governed by [AgenticInstructions/SKILL-Naming.md](AgenticInstructions/SKILL-Naming.md)
and [BannedWordsVocabulary.md](BannedWordsVocabulary.md).

---

## 1. Language Standard Selection — Enforced C++20 (`cxx_std_20`)

*Legend — Toolchain Risk: ✔️ = lowest (best) · Modularity / Compile-Time Power: 🔴 = highest (best).*

| Standard | Concepts & `std::span` | Windows (MSVC / Zig 0.13) | Linux (GCC 12+ / Clang 16+) | Android NDK (Clang) | Toolchain Risk | Quality | Rank |
| -------- | ---------------------- | ------------------------- | --------------------------- | ------------------- | -------------- | ------- | ---- |
| **C++20 (`cxx_std_20`)** | 🔴 | 🔴 | 🔴 | 🔴 | ✔️ | ⭐⭐⭐⭐⭐ | 🥇 🏆 |
| C++23 (`cxx_std_23`)     | 🔴 | 🚩 | 🚩 *(GCC 12 partial `<expected>`)* | 🚩 | 🚩 | ⭐⭐⭐⭐  | 🥈   |
| C++17 (`cxx_std_17`)     | ✔️ | 🔴 | 🔴 | 🔴 | ✔️ | ⭐⭐     | 🥉   |

⭐ **Selected Baseline:** **ISO C++20 (`CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`)** across all targets.
- Provides first-class **Concepts (`concept` / `requires`)**, **`consteval` / `constexpr`**, **`std::span`**, **`<bit>`** (`std::popcount`, `std::countr_zero`), **`<numbers>`**, **`<source_location>`**, designated initializers, and three-way comparison (`<=>`) while compiling cleanly across MSVC 2022, Zig 0.13.0 (`x86_64-windows-gnu`), Debian GCC 12+, Clang 16+, and Android NDK Clang.

---

## 2. Explicit Function Prototyping Discipline

Every header (`.h`) separates **interface declarations (function prototypes)** from **inline or template definitions**:

1. **Prototype Section First**: Declare all free functions and member functions in a dedicated `FUNCTION PROTOTYPES` block with explicit parameter names, `[[nodiscard]]` on pure queries/tokens/allocations, `constexpr` where evaluable at compile time, and `noexcept` on non-throwing hot paths.
2. **Template / Inline Implementations Below**: Place `inline` and `template` bodies in a separate `TEMPLATE IMPLEMENTATIONS` section at the bottom of the header (or `.inl` when large), keeping the prototype block scannable in one screen.
3. **No Implicit Conversions**: Mark single-argument constructors and conversion operators `explicit`.
4. **Attributes**:
   - `[[nodiscard]]` — mandatory on token issuers, authentication checks, memory acquisitions, and math/solver outputs.
   - `noexcept` — mandatory on allocators, slot recyclers, math primitives, and per-tick routing functions.

```cpp
//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr bool ValidateSlotCapacity(uint32_t RequestedSlots, uint32_t MaximumSlots) noexcept;

template <InputSignalSink SinkType>
std::size_t DrainSignals(InputExchange& Exchange, SinkType&& DestinationSink) noexcept;
```

---

## 3. Concept-Constrained Templating & Modular Design

Frontier achieves high modularity **without virtual-call pointer chasing (`vtable` misses)** on simulation,
input, geometry, or render paths:

1. **C++20 `concept` Contracts Over SFINAE & `virtual`**:
   Constrain every template parameter with a named domain `concept` rather than unconstrained `typename T` or `std::enable_if`:

   ```cpp
   template <typename ReceiverType>
   concept InputSignalSink = requires(ReceiverType& Receiver, const InputSignalPacket& Packet)
   {
       { Receiver.AcceptInputSignal(Packet) } noexcept -> std::same_as<void>;
   };
   ```

2. **Static Policy Parameterization**:
   Parameterize hardware device interfaces, coordinate precisions (`float` vs `double`), and memory allocators via concepts so unit validations (`Exhibits/`) can substitute deterministic headless windows (`HeadlessPlatformWindow`) at zero runtime cost.
3. **Shared Resource / Multi-Instance Idiom**:
   A modular type owns shared GPU or session resources **once**; per-instance parameters are supplied at invocation/encoding time via lightweight POD structs or push-constants.
4. **Compile-Time Hygiene**:
   Keep heavy third-party headers (`vulkan.h`, `Jolt.h`, `imgui.h`, `toml.hpp`) confined to `.cpp` files or dedicated hardware-boundary translation units; forward-declare opaque handles in public headers.

---

## 4. Build-Profile Architecture (`Editor` · `Development` · `Shipping`)

Exactly one profile is active per binary, enforced via `static_assert` in `Engine/Configuration/BuildConfiguration.h`:

| Profile Macro | CMake `-DFRONTIER_PROFILE=` | `Engine/Editor/` Target | Diagnostics / Assertions | Packaging & CI |
| ------------- | --------------------------- | ----------------------- | ------------------------ | -------------- |
| `FRONTIER_PROFILE_EDITOR=1`      | `Editor`      | 🟢 Compiled & linked | 🟢 Enabled | ✔️ Runs in-place (no binary packaging) |
| `FRONTIER_PROFILE_DEVELOPMENT=1` | `Development` | 🔴 Excluded          | 🟢 Enabled | ✔️ Runs in-place (fast compile flags, no packaging) |
| `FRONTIER_PROFILE_SHIPPING=1`    | `Shipping`    | 🔴 Excluded          | 🔴 Stripped | 🔴 Standalone packaged binary + release CI gate |

- **Prefer `if constexpr (BuildConfiguration::EditorIncluded)`** inside shared templates/functions so the compiler type-checks both branches cleanly.
- Reserve preprocessor `#if FRONTIER_EDITOR_ENABLED` strictly for conditional `#include` directives and struct member stripping.

---

## 5. Memory, Determinism & Error Handling

- **Zero Raw `new` / `delete`**: Allocate bulk lifetime-grouped memory through `RegionAllocator`, recycled indexed instances through `IndexedSlotRecycler`, and stale-detecting handles through `TokenIssuer` + `TokenAuthentication`.
- **Non-Owning Views**: Pass contiguous sequences via `std::span<const T>` or `PackedBits` — never copy `std::vector` across call boundaries.
- **No Hot-Path Exceptions or RTTI**: Engine runtime ticks, physics, audio mixing, input routing, and GPU command encoding never throw exceptions and never use `dynamic_cast` / `typeid`. Report recoverable failures via explicit status enums or result structs.

---

## 6. Cross-Platform & `.slang` GPU Interop Rules

1. **Target Matrix**: Every engine module (`Engine/*`) must compile cleanly on **Windows (x64/ARM64)**, **Linux (X11/Wayland, x64/ARM64)**, and **Android (NDK ARM64/x64)**.
2. **OS Header Isolation**: `<windows.h>`, `<X11/Xlib.h>`, `<wayland-client.h>`, and `<android/native_window.h>` may appear **only** inside platform-specific `*Win32.cpp`, `*X11.cpp`, `*Wayland.cpp`, `*Android.cpp`, or hardware-endpoint `.cpp` translation units.
3. **SIMD Portability**: Guard x86 intrinsics (`<immintrin.h>`) behind `#if defined(__x86_64__) || defined(_M_X64)` and provide ARM NEON (`<arm_neon.h>`) + portable scalar `constexpr` paths so Android ARM64 always builds cleanly.
4. **C++20 ↔ `.slang` Struct Parity**: Structures shared with `.slang` shaders use explicit fixed-width types (`uint32_t`, `float`), `alignas(16)`, and mandatory `static_assert(sizeof(...) == ExpectedBytes)` + `static_assert(offsetof(...) == ExpectedOffset)`.
