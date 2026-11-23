//============================================================================================================================================
//                                                       SKETCHANALYSIS.CPP
//============================================================================================================================================

#include "SlateShape/Sketch/SketchAnalysis/Api/SketchAnalysis.h"

#include <algorithm>
#include <tuple>

namespace Slate
{

namespace
{
    std::tuple<std::uint32_t, std::uint32_t, std::uint32_t> ConstraintKey(const ConstraintSpecification& Declared)
    {
        const std::uint32_t Subject = static_cast<std::uint32_t>(Declared.Subject);
        const std::uint32_t Primary = Declared.Primary.Subject == ReferenceSubject::SketchCurve
            ? Declared.Primary.SketchCurve.IssuedIndex
            : Declared.Primary.Subject == ReferenceSubject::SketchPoint
                ? Declared.Primary.SketchPoint.IssuedIndex : 0u;
        const std::uint32_t Secondary = Declared.Secondary.Subject == ReferenceSubject::SketchCurve
            ? Declared.Secondary.SketchCurve.IssuedIndex
            : Declared.Secondary.Subject == ReferenceSubject::SketchPoint
                ? Declared.Secondary.SketchPoint.IssuedIndex : 0u;
        return { Subject, std::min(Primary, Secondary), std::max(Primary, Secondary) };
    }
}

Deliver<SketchAnalysis> AnalyseSketch(const SketchStructure& Declared)
{
    if (!Declared.Declared())
        return Deliver<SketchAnalysis>::Refuse({ RefusalReason::ContentUnsupported, "the sketch is not declared" });

    SketchAnalysis Analysed = {};
    Analysed.DegreeOfFreedom = static_cast<std::uint32_t>(Declared.Curves().size() * 4u + Declared.Profiles().size() * 2u);

    std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> Seen;
    Seen.reserve(Declared.Constraints().size());
    for (std::uint32_t ConstraintIndex = 1u; ConstraintIndex <= Declared.Constraints().size(); ++ConstraintIndex)
    {
        const ConstraintSpecification& Constraint = Declared.Constraints()[ConstraintIndex - 1u];
        ConstraintFinding Finding = {};
        Finding.Subject = { ConstraintIndex };
        Finding.Conflicting = !Constraint.Declared();
        const auto Key = ConstraintKey(Constraint);
        Finding.Repeated = std::find(Seen.begin(), Seen.end(), Key) != Seen.end();
        Seen.push_back(Key);
        if (Finding.Conflicting)
            Analysed.Solvable = false;
        if (Analysed.DegreeOfFreedom > 0u)
            --Analysed.DegreeOfFreedom;
        Analysed.Findings.push_back(Finding);
    }

    for (const DimensionSpecification& Dimension : Declared.Dimensions())
        if (Dimension.Declared() && Analysed.DegreeOfFreedom > 0u)
            --Analysed.DegreeOfFreedom;

    return Deliver<SketchAnalysis>::Result(Analysed);
}

} // namespace Slate
