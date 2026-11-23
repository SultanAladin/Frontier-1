//============================================================================================================================================
//                                                         SEAMSPECIFICATION.CPP
//============================================================================================================================================
// 🧩 Two separately stored sets, and the reclamation that reaches only one of them.

#include "SlateCompute/Compute/SeamSpecification/Api/SeamSpecification.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      LOCATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

std::size_t Located(const std::vector<SeamEdge>& Held, SeamEdge Sought)
{
    for (std::size_t Index = 0u; Index < Held.size(); ++Index)
    {
        if (Held[Index].MinimumVertex == Sought.MinimumVertex
         && Held[Index].MaximumVertex == Sought.MaximumVertex)
        {
            return Index;
        }
    }

    return Held.size();
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   AUTHORED SEAMS
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> SeamSpecification::DeclareAuthored(std::uint32_t FirstVertex, std::uint32_t SecondVertex)
{
    if (FirstVertex == SecondVertex)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "one vertex is not an edge" });

    const SeamEdge Declaring = DeclareEdge(FirstVertex, SecondVertex);

    if (Located(AuthoredEdges, Declaring) != AuthoredEdges.size())
        return Deliver<bool>::Result(true);

    AuthoredEdges.push_back(Declaring);
    ++AuthoredRevision;

    return Deliver<bool>::Result(true);
}

Deliver<bool> SeamSpecification::RemoveAuthored(std::uint32_t FirstVertex, std::uint32_t SecondVertex)
{
    const SeamEdge     Sought  = DeclareEdge(FirstVertex, SecondVertex);
    const std::size_t  Located_ = Located(AuthoredEdges, Sought);

    if (Located_ == AuthoredEdges.size())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no authored seam runs there" });

    AuthoredEdges.erase(AuthoredEdges.begin() + static_cast<std::ptrdiff_t>(Located_));
    ++AuthoredRevision;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DERIVED SEAMS
//------------------------------------------------------------------------------------------------------------------------

void SeamSpecification::DeclareDerived(std::uint32_t FirstVertex, std::uint32_t SecondVertex)
{
    if (FirstVertex == SecondVertex)
        return;

    const SeamEdge Declaring = DeclareEdge(FirstVertex, SecondVertex);

    // 📝 A derived seam that duplicates an authored one is dropped rather than recorded. It cuts nothing the
    //    authored set had not already cut, and reporting it would tell the artist the partitioner added a seam
    //    they marked themselves.
    if (Located(AuthoredEdges, Declaring) != AuthoredEdges.size())
        return;

    if (Located(DerivedEdges, Declaring) != DerivedEdges.size())
        return;

    DerivedEdges.push_back(Declaring);
}

void SeamSpecification::ReclaimDerived()
{
    // 🔴 Reaches the derived set only. The authored set and its revision are untouched, which is `68` §2's
    //    whole guarantee expressed as the one line that could break it.
    DerivedEdges.clear();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

bool SeamSpecification::SeamDeclared(std::uint32_t FirstVertex, std::uint32_t SecondVertex) const
{
    const SeamEdge Sought = DeclareEdge(FirstVertex, SecondVertex);

    return Located(AuthoredEdges, Sought) != AuthoredEdges.size()
        || Located(DerivedEdges,  Sought) != DerivedEdges.size();
}

const std::vector<SeamEdge>& SeamSpecification::Authored() const { return AuthoredEdges; }
const std::vector<SeamEdge>& SeamSpecification::Derived() const  { return DerivedEdges;  }
std::uint64_t                SeamSpecification::Revision() const { return AuthoredRevision; }

std::uint32_t SeamSpecification::AuthoredCount() const { return static_cast<std::uint32_t>(AuthoredEdges.size()); }
std::uint32_t SeamSpecification::DerivedCount() const  { return static_cast<std::uint32_t>(DerivedEdges.size());  }

}   // namespace Slate
