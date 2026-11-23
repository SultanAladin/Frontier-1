/*==========================================================================================================================================
                                                           TRANSIENT TARGET ATLAS
                                     ------------------------------------------------------------------
                                 Lifetime spans over the submission order, packed into aliased memory bins.
==========================================================================================================================================*/

// 🧩 Transient target atlas: lifetime spans over the submission order, packed into aliased memory bins.

#pragma once

#include "SubmissionOrder.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct AtlasPlacement
{
    std::string Name;        // [-]
    uint32_t    Bin;         // [idx] - Aliasing bin; unused/persistent targets get UINT32_MAX
    uint32_t    FirstUse;    // [position] - First submission position touching the target
    uint32_t    LastUse;     // [position] - Last submission position touching the target
};

struct ResolvedTransientAtlas
{
    std::vector<AtlasPlacement> Placements;     // [targets] - One per declaration, declaration order
    std::vector<uint64_t>       BinBytes;       // [bytes] - Size of each bin (largest occupant)
    uint64_t                    TotalBytes     = 0;  // [bytes] - Sum of bins (what the atlas allocates)
    uint64_t                    UnaliasedBytes = 0;  // [bytes] - Sum of transient targets (no-atlas cost)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Lifetimes are submission-position spans [first touch, last touch]. Two targets may share a bin only when
// their spans are strictly disjoint (a target last used at position p frees its bin for first use after p,
// so a unit that reads A while writing B never aliases them). Persistent targets are excluded by design.
// Returns false when a declared transient target is never used or a used target has no size declaration.
[[nodiscard]] bool ResolveTransientTargetAtlas(const std::vector<SubmissionUnitDeclaration>&  Units,
                                               const ResolvedSubmissionOrder&                 Order,
                                               const std::vector<RenderTargetDeclaration>&    Targets,
                                               ResolvedTransientAtlas&                        Out,
                                               std::string&                                   Diagnostic);

} // namespace Frontier
