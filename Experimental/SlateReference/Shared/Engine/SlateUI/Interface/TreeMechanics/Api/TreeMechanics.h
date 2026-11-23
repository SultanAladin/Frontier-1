//============================================================================================================================================
//                                                          TREEMECHANICS.H
//============================================================================================================================================
// 🧩 Shared stable-row identity, persistent selection, and flattened-tree visibility mechanics.

#pragma once

#include <cstdint>

namespace Slate
{

using StableRowIdentity = std::uint64_t;
inline constexpr std::uint32_t NoTreeParent = 0xFFFFFFFFu;

struct SelectionGesture
{
    bool Range  = false;
    bool Toggle = false;
};

/// 🧩 Mutates host-owned persistent membership without owning domain rows.
class SelectionSet
{
public:
    static void Apply(bool* Membership, std::uint32_t Count, std::uint32_t& Anchor,
                      std::uint32_t Target, const bool* Presented, SelectionGesture Gesture);

    /// 🧩 Resolves a selected primary after a toggle; restores Fallback when the set would be empty.
    static std::uint32_t Primary(bool* Membership, std::uint32_t Count, std::uint32_t Fallback);
};

/// 🧩 Resolves occupancy and filtering for a flattened tree whose domain model remains caller-owned.
class SceneTreePolicy
{
public:
    /// 🧩 Scene drops only parent; a row cannot become a child of its own subtree.
    static bool AllowsParent(std::uint32_t Source, std::uint32_t Destination,
                             const std::uint32_t* Depths, std::uint32_t Count);
};

class TextureStackPolicy
{
public:
    /// 🧩 Texture drops preserve compositing order and reject destinations inside the carried subtree.
    static bool AllowsPlacement(std::uint32_t Source, std::uint32_t Destination,
                                const std::uint32_t* Depths, std::uint32_t Count);
};

class VisibleTree
{
public:
    /// 🧩 Multiplies the eased disclosure of every ancestor, guarding malformed cycles.
    static float AncestorOccupancy(const std::uint32_t* Parents, const float* Expansion,
                                   std::uint32_t Count, std::uint32_t Index);

    /// 🧩 Resolves all visible rows. Filtering can retain matching ancestors without changing domain order.
    static void Resolve(const std::uint32_t* Parents, const float* Expansion, const bool* Retained,
                        std::uint32_t Count, bool Filtering, bool IncludeMatchingAncestors,
                        bool* Presented, float* Occupancy);
};

}   // namespace Slate
