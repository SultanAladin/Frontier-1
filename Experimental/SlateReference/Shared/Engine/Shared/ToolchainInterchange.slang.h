//============================================================================================================================================
//                                                      TOOLCHAININTERCHANGE.SLANG.H
//============================================================================================================================================
// 🧩 Supplies the type, parameter-direction and intrinsic spellings each toolchain lacks.

#pragma once

#include "Foundation/Toolchain.h"

// 📝 Everything under Shared/ is compiled once by the C++ toolchain and once by the shader toolchain. This
//    file is the only place the two spellings are reconciled. A Shared/ entry point that reaches for a
//    toolchain-specific spelling directly has stopped being shared source, and ParityRunner cannot cover it.
//
// 📝 🔴 Named `Prelude` until it was read rather than assumed. A prelude states only WHERE a file sits — first,
//    ahead of the rest — which is true of any header and distinguishes nothing; `SKILL-Naming` §① asks for the
//    mechanism instead. The mechanism is a **standardised translation across two toolchains**, which is exactly
//    what the `Interchange` role names, on the same terms as `PlatformInterchange` translating one surface over
//    three operating systems. `Exchange` was the near miss: it names transfer across a system, vendor or hardware
//    edge, and nothing is transferred here — no bytes cross at run time, because both sides are compiled from
//    this one source and the file has ceased to exist by the time either runs.
//
// 📝 🔴 The scalar widths and the constant spelling are **not** declared here. `Foundation/` declares its capacities
//    in those widths and depends on nothing, so it cannot read them from this file; they live in
//    `Foundation/Toolchain.h` and are included from there. What remains below is what only shared *source*
//    needs — parameter directions, the component widths, and the intrinsics whose spellings differ.

#if defined(SLATE_SHADER_TOOLCHAIN)

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHADER TOOLCHAIN
//------------------------------------------------------------------------------------------------------------------------

#define SLATE_SHARED
#define SLATE_CONSTEXPR
#define SLATE_OUT(TypeName)                            out TypeName
#define SLATE_INOUT(TypeName)                          inout TypeName
#define SLATE_INOUT_SPAN(TypeName, SpanName, Capacity) inout TypeName SpanName[Capacity]

// 📝 The component spellings a resident surface is declared and sampled through, and the **only** ones. They
//    are device-side alone and deliberately have no host counterpart: everything under `Shared/` is scalar
//    throughout, so a host form would be a spelling nothing in shared source is permitted to reach for. A surface
//    of four components must say so at its declaration — `Texture2D<Real32>` samples one component and silently
//    delivers red where three were meant, which reads as a monochrome sky rather than as a mistake.
//
// 📝 🔴 `Real32x3` was USED before it was DECLARED. `WorkspaceOverlayFragment.slang`'s analytic ground names it eight
//    times — the eye, the camera basis, the pixel's ray and the lattice tone are all three-component — and this
//    file declared only the two- and four-component widths, so slangc rejected the unit with eight
//    `undefined identifier 'Real32x3'` at 30015 and the whole SlateVulkan shader stage threw. Nothing caught it
//    earlier because the sandbox gate is `g++ -fsyntax-only` over the HOST toolchain, which never enters this
//    branch of the file at all: the shader spellings are compiled by slangc alone, on Windows alone. A spelling
//    added to one branch and not declared is invisible to every check that runs here.
typedef float2   Real32x2;
typedef float3   Real32x3;
typedef float4   Real32x4;

// 📝 🔴 The two ordinal widths, which share the component widths' reasoning above — device-side alone, with no
//    host counterpart, because nothing under `Shared/` dispatches or addresses a surface.
//
//    `Unsigned32x3` is what `SV_DispatchThreadID` is delivered as; the toolchain refuses a three-element array in
//    its place. `Unsigned32x2` is what a writable surface is **addressed** through: the toolchain takes one operand
//    between the brackets and not two, so `Surface[X, Y]` is a call carrying an argument too many rather
//    than the pair it reads as, and the two ordinates are named into this width first. Both are declared once here
//    rather than reached for at each entry point, which is what makes every entry point the same shape as the rest.
typedef uint2    Unsigned32x2;
typedef uint3    Unsigned32x3;

Real64 Magnitude(Real64 Operand)
{
    return abs(Operand);
}

Real64 SquareRoot(Real64 Operand)
{
    return sqrt(Operand);
}

Real64 Sine(Real64 Operand)
{
    return sin(Operand);
}

Real64 Cosine(Real64 Operand)
{
    return cos(Operand);
}

Real64 Exponential(Real64 Operand)
{
    return exp(Operand);
}

Real64 ArcCosine(Real64 Operand)
{
    return acos(Operand);
}

Real64 ArcTangentQuadrant(Real64 Numerator, Real64 Denominator)
{
    return atan2(Numerator, Denominator);
}

Real64 Power(Real64 Operand, Real64 Exponent)
{
    return pow(Operand, Exponent);
}

Real64 BoundedMagnitude(Real64 Operand, Real64 Lower, Real64 Upper)
{
    return Operand < Lower ? Lower : (Operand > Upper ? Upper : Operand);
}

Unsigned32 ReversedBits(Unsigned32 Operand)
{
    return reversebits(Operand);
}

#else

//------------------------------------------------------------------------------------------------------------------------
//                                                    HOST TOOLCHAIN
//------------------------------------------------------------------------------------------------------------------------

#include <cmath>

#define SLATE_SHARED                                   inline
#define SLATE_CONSTEXPR                                constexpr
#define SLATE_OUT(TypeName)                            TypeName&
#define SLATE_INOUT(TypeName)                          TypeName&
#define SLATE_INOUT_SPAN(TypeName, SpanName, Capacity) TypeName (&SpanName)[Capacity]

SLATE_SHARED Real64 Magnitude(Real64 Operand)
{
    return std::fabs(Operand);
}

SLATE_SHARED Real64 SquareRoot(Real64 Operand)
{
    return std::sqrt(Operand);
}

SLATE_SHARED Real64 Sine(Real64 Operand)
{
    return std::sin(Operand);
}

SLATE_SHARED Real64 Cosine(Real64 Operand)
{
    return std::cos(Operand);
}

SLATE_SHARED Real64 Exponential(Real64 Operand)
{
    return std::exp(Operand);
}

SLATE_SHARED Real64 ArcCosine(Real64 Operand)
{
    return std::acos(Operand);
}

SLATE_SHARED Real64 ArcTangentQuadrant(Real64 Numerator, Real64 Denominator)
{
    return std::atan2(Numerator, Denominator);
}

// 📝 The host's own spelling. Declared beside every other intrinsic rather than reached for at the two shared
//    sites that need it, because a shared file naming `std::pow` directly has stopped being shared source.
SLATE_SHARED Real64 Power(Real64 Operand, Real64 Exponent)
{
    return std::pow(Operand, Exponent);
}

// 📝 The bound is spelled here rather than reached for per toolchain because the shader form's `clamp` and the
//    host's nested conditional disagree on a non-finite operand, and a zenith cosine incoming as one is exactly
//    the input a parity comparison is meant to catch rather than to silently normalise.
SLATE_SHARED Real64 BoundedMagnitude(Real64 Operand, Real64 Lower, Real64 Upper)
{
    return Operand < Lower ? Lower : (Operand > Upper ? Upper : Operand);
}

// 📝 The shader toolchain supplies a bit reversal and the host does not, which is exactly the divergence this
//    file exists to absorb. The host form reverses in five constant-width exchanges rather than in a loop, so
//    both forms are branch-free and neither can diverge on the ordinal it is given.
SLATE_SHARED Unsigned32 ReversedBits(Unsigned32 Operand)
{
    Operand = ((Operand & 0x55555555u) << 1)  | ((Operand & 0xAAAAAAAAu) >> 1);
    Operand = ((Operand & 0x33333333u) << 2)  | ((Operand & 0xCCCCCCCCu) >> 2);
    Operand = ((Operand & 0x0F0F0F0Fu) << 4)  | ((Operand & 0xF0F0F0F0u) >> 4);
    Operand = ((Operand & 0x00FF00FFu) << 8)  | ((Operand & 0xFF00FF00u) >> 8);

    return (Operand << 16) | (Operand >> 16);
}

#endif
