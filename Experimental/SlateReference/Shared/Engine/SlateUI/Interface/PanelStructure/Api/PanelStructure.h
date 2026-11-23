//============================================================================================================================================
//                                                          PANELSTRUCTURE.H
//============================================================================================================================================
// 🧩 A bounded binary partition of one workspace into viewport, UV, outliner, property and vacant panels.

#pragma once

#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   PARTITION DECLARATIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one leaf panel presents.
/// tag   guarantee
enum class PanelSubject : std::uint32_t
{
    Viewport       = 0u,   // [-] - three-dimensional scene presentation
    Uv             = 1u,   // [-] - selected geometry's UV presentation
    Outliner       = 2u,   // [-] - editor scene outline
    Properties     = 3u,   // [-] - selected record's properties
    Vacant         = 4u,   // [-] - panel chooser
    Texturing   = 5u,   // [-] - the texture-texture layer stack (appended, so 0-4 never move)
    ParametricTools = 6u,  // [-] - the CAD construction catalogue and its settings
    SketchDirectory = 7u, // [-] - the CAD/sketch semantic directory
    SubjectCount   = 8u    // [-] - closed count, never a subject
};

/// 🧩 Which display axis a division partitions.
/// tag   guarantee
enum class PanelDivisionAxis : std::uint32_t
{
    X     = 0u,   // [-] - left and right leaves
    Y    = 1u,   // [-] - upper and lower leaves
    AxisCount = 2u    // [-] - closed count, never an axis
};

/// 🧩 Which side of a division receives a newly created vacant panel.
/// tag   guarantee
enum class PanelDivisionSide : std::uint32_t
{
    Minimum     = 0u,   // [-] - left or upper side
    Maximum      = 1u,   // [-] - right or lower side
    SideCount = 2u    // [-] - closed count, never a side
};

/// 🧩 One occupied slot in the binary workspace partition.
/// tag   guarantee, nonallocating, nonthrowing
struct PanelRecord
{
    bool               Occupied       = false;                      // [-] - this slot participates in the partition
    bool               Divided        = false;                      // [-] - false is a leaf carrying Subject
    PanelSubject       Subject        = PanelSubject::Vacant;       // [-] - what a leaf presents
    PanelDivisionAxis  Axis           = PanelDivisionAxis::X;   // [-] - how a divided slot separates descendants
    float              MinimumFraction  = 0.5f;                       // [-] - fraction assigned to Minimum
    std::uint32_t      Minimum   = 0u;                         // [-] - first side of a divided slot
    std::uint32_t      Maximum    = 0u;                         // [-] - second side of a divided slot
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PARTITION OWNERSHIP
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Owns the artist's bounded panel partition while `EditorPanel` only presents and edits it.
/// tag   owning, nonallocating, nonthrowing
class PanelStructure
{
public:

    static constexpr std::uint32_t RecordLimit = 11u;   // [-] - five simultaneous divisions; never allocated
    static constexpr std::uint32_t RootIndex   = 0u;    // [-] - stable root slot

    /// 🧩 Returns the partition to one leaf carrying the declared subject.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void ConstructPanelPartition(PanelSubject InitialSubject = PanelSubject::Viewport);

    /// 🧩 Replaces one leaf by an equal binary division and applies a vacant leaf on the requested side.
    /// out   Result  [-]  refuses for a stale or divided ordinal, or when two slots are unavailable
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Divide(std::uint32_t LeafIndex,
                         PanelDivisionAxis Axis,
                         PanelDivisionSide VacantSide);

    /// 🧩 Removes one leaf and promotes the opposite side into its enclosing slot.
    /// out   Result  [-]  refuses for a stale ordinal and for the sole root leaf
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Withdraw(std::uint32_t LeafIndex);

    /// 🧩 Changes what one leaf presents.
    /// out   Result  [-]  refuses for a stale or divided ordinal and an unsupported subject
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Assign(std::uint32_t LeafIndex, PanelSubject Subject);

    /// 🧩 Changes one division's least-side fraction, clamped to the reference's five-percent limits.
    /// out   Result  [-]  refuses for a stale leaf ordinal
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Proportion(std::uint32_t DivisionIndex, float MinimumFraction);

    /// 🧩 Reads one occupied record; an unoccupied ordinal refuses as stale.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<PanelRecord> Current(std::uint32_t Index) const;

    /// 🧩 Whether the partition contains more than its sole root leaf.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool RemovalAccepted() const;

    /// 🧩 Returns every slot to its default condition.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Reset();

private:

    bool Encloses(std::uint32_t BranchIndex,
                  std::uint32_t SeekingIndex,
                  std::uint32_t& EnclosingIndex,
                  bool& MinimumSide) const;
    std::uint32_t TakeVacant();

    PanelRecord Records[RecordLimit] = {};   // [-] - bounded partition storage
};

}   // namespace Slate
