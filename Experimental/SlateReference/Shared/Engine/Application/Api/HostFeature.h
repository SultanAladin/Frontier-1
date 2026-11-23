//============================================================================================================================================
//                                                              HOSTFEATURE.H
//============================================================================================================================================

#pragma once

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     PRODUCT SEPARATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 One host source, several products. `Engine/Application/Module.toml`'s [product] table names one
//    feature macro per product and `Build/Construct.ps1` supplies it as /D. This header reads that macro
//    and is the ONLY place that does — a second reader is a second statement of what a product contains.
//
// 🔴 The predecessor of this header declared the same masks, was never handed a macro by the build, and
//    was never read by any caller. Both ends were dead, so an agent looking for the live mechanism
//    correctly concluded there was none and wrote its own camera, sky and CAD editor. The #error below
//    is what makes that failure impossible to repeat: a build that forgets the macro does not produce a
//    featureless host, it produces a diagnostic naming this file.

constexpr std::uint32_t FeatureWorld      = 1u << 0u;   // [-] - camera, sky, lighting, ground lattice
constexpr std::uint32_t FeatureCodexScene = 1u << 1u;   // [-] - scene import, codex activation
constexpr std::uint32_t FeatureTexture    = 1u << 2u;   // [-] - texturing: brushes, layer stack
constexpr std::uint32_t FeatureParametric = 1u << 3u;   // [-] - parametric CAD: sketch, constraints, features

#if defined(SLATE_COMBINED_AUTHORING)

constexpr std::uint32_t HostFeatures = FeatureWorld | FeatureCodexScene | FeatureTexture | FeatureParametric;
constexpr const char*   HostProduct  = "Slate Authoring";

#elif defined(SLATE_TEXTURE_AUTHORING)

constexpr std::uint32_t HostFeatures = FeatureWorld | FeatureCodexScene | FeatureTexture;
constexpr const char*   HostProduct  = "Texture Authoring";

#elif defined(SLATE_PARAMETRIC_AUTHORING)

constexpr std::uint32_t HostFeatures = FeatureWorld | FeatureCodexScene | FeatureParametric;
constexpr const char*   HostProduct  = "Parametric Authoring";

#else

#error "No product macro was defined. Engine/Application/Module.toml's [product] table declares one per product and Build/Construct.ps1 supplies it as /D. A host reaching this branch would have compiled with no features at all."

#endif

/// 🧩 Whether the product being built carries a capability.
/// in    Feature  [-]  one of the Feature… constants above, or several combined
/// out   Carried  [-]  true when every named capability is in this product
/// note  🔴 Consumed through `if constexpr`, not `#if`. The disabled branch is still parsed and
///        type-checked, so a change to a capability this product excludes still has to compile — which
///        is what stops a texture-only edit from silently breaking the parametric build. The optimiser
///        drops the dead branch, so the arrangement costs nothing at run time.
/// note  ⚠️ `#if` is correct in exactly one situation: an `#include`, or a member whose TYPE is absent
///        from this product's link. A type that is not linked cannot be named, so no `if constexpr` can
///        guard it. Everything else — tick steps, panel registration, input routing — uses this.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr bool HostHasFeature(std::uint32_t Feature)
{
    return (HostFeatures & Feature) == Feature;
}

// 📝 The preprocessor spellings, for the two situations `if constexpr` cannot serve. They are derived
//    from the same macros rather than declared beside them, so a product cannot carry a feature by one
//    spelling and lack it by the other.
#if defined(SLATE_COMBINED_AUTHORING) || defined(SLATE_TEXTURE_AUTHORING)
#define SLATE_HAS_TEXTURE 1
#else
#define SLATE_HAS_TEXTURE 0
#endif

#if defined(SLATE_COMBINED_AUTHORING) || defined(SLATE_PARAMETRIC_AUTHORING)
#define SLATE_HAS_PARAMETRIC 1
#else
#define SLATE_HAS_PARAMETRIC 0
#endif

} // namespace Slate
