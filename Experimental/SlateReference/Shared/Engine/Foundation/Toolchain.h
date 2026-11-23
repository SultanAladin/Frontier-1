//============================================================================================================================================
//                                                           TOOLCHAINGUARANTEE.H
//============================================================================================================================================
// 🧩 The constant, assertion and scalar spellings each toolchain lacks. Depends on nothing; read by Foundation/ and by Shared/.
//
// 📝 The **scalar** widths alone live here, because a capacity is what `Foundation/` declares and a capacity is a
//    scalar. The component and ordinal widths a device surface is declared, sampled and addressed through are
//    `Shared/ToolchainInterchange.slang.h`'s — nothing in `Foundation/` reaches for one, and declaring them here would place a
//    device-only spelling in the file that depends on nothing.

#pragma once

// 📝 🔴 `00` §2 places a spelling two readers both need at the root of the dependency order, and this one has two:
//    `Foundation/`, which declares the capacities in the scalar widths, and `Shared/`, which computes against them.
//    It cannot live in `Shared/ToolchainInterchange.slang.h` — `Shared/` already depends on `Foundation/`, so a guarantee header
//    reading a spelling from there is that edge pointing backwards. It lives here instead, and `ToolchainInterchange.slang.h`
//    reads it from here rather than declaring a second copy that would agree with this one only until one of the
//    two was amended.
//
// ⚠️ Everything below is declared at **global** scope and not inside `Slate`. A width is not a Slate concept; it
//    is what the toolchain calls its own arithmetic, and nesting it would make every guarantee declaration spell
//    the namespace twice.

#if defined(SLATE_SHADER_TOOLCHAIN)

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHADER TOOLCHAIN
//------------------------------------------------------------------------------------------------------------------------

// 📝 🔴 A constant at namespace scope, which `SLATE_CONSTEXPR` does not cover: that macro expands to nothing under
//    this toolchain, so a declaration wearing it becomes a mutable global the shader toolchain reads as a uniform
//    — a capacity the host folded at compilation becomes one the device expects somebody to upload. `static const`
//    is the spelling that folds here, and it is already what the entry points under `Shader/` reach for directly.
#define SLATE_CONSTANT static const

// 📝 🔴 This toolchain has no compile-time assertion. The condition is checked by the host translation of the same
//    source, which is not a weaker gate: everything reaching this file is compiled once per toolchain by
//    construction, and the host half runs on every build. Expanding to nothing is deliberate — a condition spelled
//    two ways is a condition true in only one of them, and that is worse than a condition checked once.
#define SLATE_STATIC_ASSERT(Condition, Message)

typedef double   Real64;
typedef float    Real32;
typedef int      Signed32;
typedef uint     Unsigned32;
typedef uint64_t Unsigned64;

#else

//------------------------------------------------------------------------------------------------------------------------
//                                                    HOST TOOLCHAIN
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>

#define SLATE_CONSTANT                          inline constexpr
#define SLATE_STATIC_ASSERT(Condition, Message) static_assert(Condition, Message)

using Real64     = double;
using Real32     = float;
using Signed32   = std::int32_t;
using Unsigned32 = std::uint32_t;
using Unsigned64 = std::uint64_t;

#endif
