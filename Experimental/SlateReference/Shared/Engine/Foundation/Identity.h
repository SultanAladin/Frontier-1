//============================================================================================================================================
//                                                            IDENTITYGUARANTEE.H
//============================================================================================================================================
// 🧩 Generational slot identity, tagged per subject so one subject's identity never passes for another's.

#pragma once

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     SUBJECT TAGS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each tag exists only to make Identity<Subject> a distinct type. A PartitionIdentity passed where an
//    OwnerIdentity is expected is a compile error, which is the whole reason the tags are declared.
struct OwnerSubject  {};
struct PartitionSubject {};
struct SurfaceSubject   {};
struct RecordingSubject {};
struct WorkSubject      {};
struct LayerSubject     {};
struct GeometrySubject  {};
struct GeometryRenderingSubject {};

//------------------------------------------------------------------------------------------------------------------------
//                                                       IDENTITY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 A slot ordinal paired with the generation the slot held when the reference was taken.
/// note  Identity is Exact. It is an unsigned integer pair, never a real number and never narrowed.
/// note  A generation of zero declares the reference absent; registered generations begin at one.
/// tag   guarantee, nonallocating, nonthrowing
template <typename Subject>
struct Identity
{
    std::uint32_t  SlotIndex    = 0u;   // [-] - index into the slot index that registered it
    std::uint32_t  SlotGeneration = 0u;   // [-] - zero declares the reference absent

    /// 🧩 Whether this reference names a slot at all.
    /// out   Declared   [-]  false for a default-constructed identity
    /// cost  ✔️
    constexpr bool IdentityDeclared() const
    {
        return SlotGeneration != 0u;
    }
};

/// 🧩 Two identities of the same subject match when both halves match.
/// cost  ✔️
template <typename Subject>
constexpr bool operator==(Identity<Subject> LeftIdentity, Identity<Subject> RightIdentity)
{
    return LeftIdentity.SlotIndex    == RightIdentity.SlotIndex
        && LeftIdentity.SlotGeneration == RightIdentity.SlotGeneration;
}

template <typename Subject>
constexpr bool operator!=(Identity<Subject> LeftIdentity, Identity<Subject> RightIdentity)
{
    return !(LeftIdentity == RightIdentity);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                THE DECLARED SUBJECTS
//------------------------------------------------------------------------------------------------------------------------

using OwnerIdentity  = Identity<OwnerSubject>;    // [-] - one owner of the document population
using PartitionIdentity = Identity<PartitionSubject>;   // [-] - one partition of one owner's topology
using SurfaceIdentity   = Identity<SurfaceSubject>;     // [-] - one texturable surface domain
using RecordingIdentity = Identity<RecordingSubject>;   // [-] - one slot of the recording rotation
using WorkIdentity      = Identity<WorkSubject>;        // [-] - one declaration in `34`'s work sequence
using LayerIdentity     = Identity<LayerSubject>;       // [-] - one entry of a surface's layer sequence
using GeometryIdentity  = Identity<GeometrySubject>;    // [-] - one authoritative geometry asset
using GeometryRenderingIdentity = Identity<GeometryRenderingSubject>; // [-] - one disposable geometry cache

// 📝 🔴 `56` §8 requires an entry to carry `10` §2.1's integer pair rather than a bare position: a collision
//    reorders a surface, and a position alone collides with whichever entry later occupies it. Tagged apart from
//    an owner because a textured layer is not one — only a placement is registered in the population.

}   // namespace Slate
