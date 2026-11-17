/*====================================================================================================================================
                                                          MODELAUDIT.H
====================================================================================================================================*/
// 🧩 Tolerance-aware topological and geometric audit of a body — the gate every operation result must pass

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Topology/ModelPartition.h"

#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

enum class AuditScope : uint8_t
{
    Topology = 0,                                                                            // T1–T5 only (mid-construction)
    Complete,                                                                                // T- and G-checks
};

// 📝 One row per check: how many subjects it examined and how many failed.
struct AuditTally
{
    const char* Code        = "";                                                            // [-] - T1…T6, G1…G6
    const char* Description = "";                                                            // [-] - What is verified
    int         Examined    = 0;                                                             // [-] - Subjects examined
    int         Failed      = 0;                                                             // [-] - Subjects failing
};

struct AuditFinding
{
    const char* Code     = "";                                                               // [-]   - Failing check
    std::string Subject  = {};                                                               // [-]   - Token label, e.g. "E4"
    std::string Detail   = {};                                                               // [-]   - Human-readable reason
    double      Measured = 0.0;                                                              // [len] - Measured deviation (0 if n/a)
    double      Allowed  = 0.0;                                                              // [len] - Allowed deviation (0 if n/a)
};

struct AuditReport
{
    std::vector<AuditTally>   Tallies  = {};                                                 // [-] - One per check, in order
    std::vector<AuditFinding> Findings = {};                                                 // [-] - Every failure
    int                       Genus    = -1;                                                 // [-] - From Euler–Poincaré (−1: not integral)
    double                    Volume   = 0.0;                                                // [len³] - Σ solid shell volumes (G6; = IntegrateBodyVolume)
    bool                      VolumeCondition = false;                                       // [-] - G6 ran, so Volume holds

    [[nodiscard]] bool SoundCondition() const noexcept { return Findings.empty(); }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] AuditReport AuditBody(const ModelPartition& Partition, BodyToken Body, AuditScope Scope = AuditScope::Complete);

// Short stable labels for tokens ("F3", "Fn7", "E12·1" when the slot was recycled).
template <typename Tag>
[[nodiscard]] std::string LabelToken(const char* Prefix, RecordToken<Tag> Token)
{
    std::string Label = std::string(Prefix) + std::to_string(Token.Slot);
    if (Token.Generation > 0) Label += "\xC2\xB7" + std::to_string(Token.Generation);
    return Label;
}

} // namespace Frontier::GeometrySpace::Parametric
