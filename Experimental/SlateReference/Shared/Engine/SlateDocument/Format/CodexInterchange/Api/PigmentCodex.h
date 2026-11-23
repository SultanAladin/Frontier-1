//============================================================================================================================================
//                                                            PIGMENTCODEX.H
//============================================================================================================================================
// 🧩 Typed fixed-surface pigment declaration, intentionally apart from image/layer processing.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Format/CodexInterchange/Api/CodexInterchange.h"

#include <cstdint>
#include <string>

namespace Slate
{

/// 🧩 The initial physical surface figures shared by every White Tea Service geometry entry.
/// note  Image channels, layer stacks, transparency, and advanced surface models do not belong to this initial
///       fixed-white declaration. The figures persist the intended common source while current raster radiance
///       remains the approved fixed-white dielectric route.
struct PigmentCodex
{
    std::string  Naming       = {};       // [-] - artist-visible material source name
    double       BaseColour[3] = { 1.0, 1.0, 1.0 };  // [-] - linear display-neutral white
    double       Roughness    = 0.32;     // [-] - future physical surface resolve input
    double       IndexOfRefraction = 1.5; // [-] - dielectric optical constant
    bool         Dielectric   = true;     // [-] - excludes metal and transmission models in this first proof
};

/// 🧩 Translates the focused PigmentCodex surface declaration into a retained typed section.
class PigmentCodexInterchange
{
public:

    /// 🧩 Adds the typed pigment-information section to a PigmentCodex document.
    Deliver<CodexDocument> EncodePigment(const PigmentCodex& Pigment,
                                         std::uint64_t      Identity,
                                         std::uint64_t      Revision) const;

    /// 🧩 Resolves the typed pigment-information section from a PigmentCodex document.
    Deliver<PigmentCodex> DecodePigment(const CodexDocument& Document) const;
};

}   // namespace Slate
