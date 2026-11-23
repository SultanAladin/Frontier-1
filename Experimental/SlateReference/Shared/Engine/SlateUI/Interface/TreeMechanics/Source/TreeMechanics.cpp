//============================================================================================================================================
//                                                         TREEMECHANICS.CPP
//============================================================================================================================================

#include "SlateUI/Interface/TreeMechanics/Api/TreeMechanics.h"

namespace Slate
{

void SelectionSet::Apply(bool* Membership, std::uint32_t Count, std::uint32_t& Anchor,
                         std::uint32_t Target, const bool* Presented, SelectionGesture Gesture)
{
    if (Membership == nullptr || Target >= Count)
        return;

    if (Gesture.Range && Anchor < Count)
    {
        for (std::uint32_t Index = 0u; Index < Count; ++Index)
            Membership[Index] = false;

        const std::uint32_t First = Anchor < Target ? Anchor : Target;
        const std::uint32_t Past  = (Anchor > Target ? Anchor : Target) + 1u;
        for (std::uint32_t Index = First; Index < Past; ++Index)
            if (Presented == nullptr || Presented[Index])
                Membership[Index] = true;
        return;
    }

    if (Gesture.Toggle)
    {
        Membership[Target] = !Membership[Target];
        Anchor = Target;
        return;
    }

    for (std::uint32_t Index = 0u; Index < Count; ++Index)
        Membership[Index] = Index == Target;
    Anchor = Target;
}

std::uint32_t SelectionSet::Primary(bool* Membership, std::uint32_t Count, std::uint32_t Fallback)
{
    if (Membership == nullptr || Count == 0u)
        return 0u;

    if (Fallback < Count && Membership[Fallback])
        return Fallback;

    for (std::uint32_t Index = 0u; Index < Count; ++Index)
        if (Membership[Index])
            return Index;

    const std::uint32_t Restored = Fallback < Count ? Fallback : 0u;
    Membership[Restored] = true;
    return Restored;
}

namespace
{

bool OutsideCarriedSubtree(std::uint32_t Source, std::uint32_t Destination,
                           const std::uint32_t* Depths, std::uint32_t Count)
{
    if (Depths == nullptr || Source >= Count || Destination >= Count || Source == Destination)
        return false;

    std::uint32_t Past = Source + 1u;
    while (Past < Count && Depths[Past] > Depths[Source])
        ++Past;
    return Destination < Source || Destination >= Past;
}

}   // namespace

bool SceneTreePolicy::AllowsParent(std::uint32_t Source, std::uint32_t Destination,
                                   const std::uint32_t* Depths, std::uint32_t Count)
{
    return OutsideCarriedSubtree(Source, Destination, Depths, Count);
}

bool TextureStackPolicy::AllowsPlacement(std::uint32_t Source, std::uint32_t Destination,
                                         const std::uint32_t* Depths, std::uint32_t Count)
{
    return OutsideCarriedSubtree(Source, Destination, Depths, Count);
}

float VisibleTree::AncestorOccupancy(const std::uint32_t* Parents, const float* Expansion,
                                     std::uint32_t Count, std::uint32_t Index)
{
    if (Parents == nullptr || Expansion == nullptr || Index >= Count)
        return 1.0f;

    float Reach = 1.0f;
    std::uint32_t Walking = Parents[Index];
    std::uint32_t Guard = 0u;
    while (Walking < Count && Guard++ < Count)
    {
        Reach *= Expansion[Walking];
        if (Reach <= 0.0f)
            return 0.0f;
        Walking = Parents[Walking];
    }
    return Reach;
}

void VisibleTree::Resolve(const std::uint32_t* Parents, const float* Expansion, const bool* Retained,
                          std::uint32_t Count, bool Filtering, bool IncludeMatchingAncestors,
                          bool* Presented, float* Occupancy)
{
    if (Presented == nullptr || Occupancy == nullptr)
        return;

    for (std::uint32_t Index = 0u; Index < Count; ++Index)
    {
        Occupancy[Index] = Filtering ? 1.0f : AncestorOccupancy(Parents, Expansion, Count, Index);
        Presented[Index] = Filtering ? (Retained == nullptr || Retained[Index]) : Occupancy[Index] > 0.001f;
    }

    if (!Filtering || !IncludeMatchingAncestors || Parents == nullptr)
        return;

    for (std::uint32_t Index = 0u; Index < Count; ++Index)
    {
        if (!Presented[Index])
            continue;

        std::uint32_t Walking = Parents[Index];
        std::uint32_t Guard = 0u;
        while (Walking < Count && Guard++ < Count)
        {
            Presented[Walking] = true;
            Walking = Parents[Walking];
        }
    }
}

}   // namespace Slate
