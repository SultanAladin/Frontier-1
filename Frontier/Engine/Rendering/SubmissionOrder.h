/*==========================================================================================================================================
                                                        SUBMISSION ORDER RESOLUTION
                                     ------------------------------------------------------------------
                                Declared unit reads/writes become a deterministic, dependency-safe sequence.
==========================================================================================================================================*/

// 🧩 Submission order resolution: declared unit reads/writes become a deterministic, dependency-safe sequence.

#pragma once

#include "RenderTarget.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// One declared unit of work. Declaration order is program order (the frame-graph convention): data
// dependencies are derived by scanning declarations, tracking each target's last writer and the readers
// since that write. After names units that must submit first for side-effect reasons data cannot express.
struct SubmissionUnitDeclaration
{
    std::string              Name;     // [-] - Unique unit name (also the After handle)
    std::vector<TargetTouch> Touches;  // [touches] - Target uses in declared order (usage class drives edges)
    std::vector<std::string> After;    // [units] - Explicit predecessors by name
};

struct ResolvedSubmissionOrder
{
    std::vector<uint32_t>    Order;           // [idx] - Declaration indices in submission sequence
    std::vector<std::string> ImportedTargets; // [targets] - Read before any write (history / external)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Derives RAW/WAW/WAR edges plus explicit After edges and topologically resolves them. Ties break toward
// declaration order, so the result is deterministic. Returns false with a named cycle (or unknown After
// unit / duplicate name) in Diagnostic; the refusal is also recorded on the Render log stream.
[[nodiscard]] bool ResolveSubmissionOrder(const std::vector<SubmissionUnitDeclaration>& Units,
                                          ResolvedSubmissionOrder&                    Out,
                                          std::string&                              Diagnostic);

} // namespace Frontier
