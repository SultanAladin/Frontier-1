/*============================================================================================================================================
                                                            OPERATIONRECORDER.H
============================================================================================================================================*/
// 🧩 Resolution-independent parametric operation recorder with Redo-Purge -> Prefix-Fold compaction

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Revision/ContentRegion.h"
#include "Engine/Workspaces/Revision/NonLinearHistory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ParametricOperationCategory : uint8_t
{
    BaselineAnchor            = 0, // [-] - Root baseline anchor entry
    NormalizedBrushSegment    = 1, // [-] - Resolution-independent [0,1]^2 stroke segment
    UVTileTransform           = 2, // [-] - Parametric UVTile offset, scale, rotation, & texel density
    ScalarChannelAdjustment   = 3, // [-] - Analytic gain/offset adjustment on a named channel
    VectorContourControlPoint = 4, // [-] - Normalized Bezier/spline contour point in [0,1]^2
    StructuredPayloadDelta    = 5  // [-] - Compact 64B-aligned parametric payload stored in ContentRegion
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParametricOperationParameters
{
    ParametricOperationCategory Category      = ParametricOperationCategory::NormalizedBrushSegment; // [-] - Parametric operation type
    int32_t                     TileU         = 0;     // [tile]  - Integer UDIM horizontal UVTile index
    int32_t                     TileV         = 0;     // [tile]  - Integer UDIM vertical UVTile index
    uint32_t                    TargetChannel = 0u;    // [idx]   - Target layer, contour, or channel ordinal
    std::array<double, 8u>      Scalars       {};      // [-]     - Normalized [0,1] coordinates, angles [rad], or gains
    char                        OperationTag[40] {};   // [-]     - Compact UTF-8 diagnostic label
};

struct ParametricRecordEntry
{
    uint32_t                      RecordOrdinal        = 0u;    // [count] - Monotonic RecordEntry index
    ParametricOperationParameters Parameters           {};      // [-]     - Resolution-independent scalar parameters
    std::size_t                   RegionByteOffset     = 0u;    // [B]     - Offset into ContentRegion for variable payload
    std::size_t                   RegionByteLength     = 0u;    // [B]     - Unpadded payload byte length
    std::size_t                   AlignedFootprintBytes = 0u;   // [B]     - 64B-aligned footprint inside ContentRegion
    bool                          AbsorbedOrPurged     = false; // [-]     - True once folded into baseline or redo-purged
};

struct ParametricBaselineSummary
{
    uint64_t                                   FoldedOperationCount  = 0ull; // [count] - Operations losslessly folded into baseline
    std::vector<ParametricOperationParameters> FoldedOperations      {};     // [-]     - Compact folded parametric baseline sequence
    double                                     CumulativeExposureEV  = 0.0;  // [EV]    - Accumulated scalar channel delta preview
};

struct CompactionReport
{
    uint32_t    PurgedRedoSteps      = 0u; // [count] - Inactive branch steps evicted in Step 1
    uint32_t    FoldedPrefixSteps    = 0u; // [count] - Oldest active steps folded into baseline in Step 2
    std::size_t BytesBeforeCompaction = 0u; // [B]    - Region + metadata footprint prior to compaction
    std::size_t BytesAfterCompaction  = 0u; // [B]    - Region + metadata footprint after compaction
};

class OperationRecorder
{
public:
    explicit OperationRecorder(std::size_t MaximumDocumentBudgetBytes = Constants::DefaultDocumentRevisionBudgetBytes);

    void Reset();
    void ConfigureBudgetCeiling(std::size_t NewBudgetBytes);

    [[nodiscard]] static ParametricOperationParameters ConstructNormalizedBrush(
        double           StartU,
        double           StartV,
        double           EndU,
        double           EndV,
        double           NormalizedRadius,
        double           LinearRed,
        double           LinearGreen,
        double           LinearBlue,
        std::string_view Tag = "NormalizedBrushSegment") noexcept;

    [[nodiscard]] static ParametricOperationParameters ConstructUVTileTransform(
        int32_t          TileU,
        int32_t          TileV,
        double           TranslateU,
        double           TranslateV,
        double           UniformScale,
        double           RotationRadians,
        double           TexelDensityPxPerMeter,
        std::string_view Tag = "UVTileTransform") noexcept;

    [[nodiscard]] static ParametricOperationParameters ConstructVectorControlPoint(
        uint32_t         ContourIndex,
        double           NormalizedU,
        double           NormalizedV,
        double           TangentU,
        double           TangentV,
        double           StrokeMillimeters,
        std::string_view Tag = "VectorControlPoint") noexcept;

    [[nodiscard]] bool RecordOperation(
        NonLinearHistory&                    History,
        const ParametricOperationParameters& Parameters,
        std::span<const std::byte>           AuxiliaryPayload,
        uint64_t                             GlobalSequenceStamp,
        CompactionReport*                    OutCompaction = nullptr);

    CompactionReport EnforceBudgetCeiling(NonLinearHistory& History, std::size_t TargetCeilingBytes);
    CompactionReport PurgeInactiveRedo(NonLinearHistory& History);
    CompactionReport FoldOldestPrefix(NonLinearHistory& History, uint32_t StepCount);

    [[nodiscard]] std::size_t                             QueryTotalFootprintBytes() const noexcept;
    [[nodiscard]] std::size_t                             QueryBudgetCeilingBytes() const noexcept { return Region.QueryMaximumBudgetBytes(); }
    [[nodiscard]] const ParametricBaselineSummary&        InspectBaseline() const noexcept { return Baseline; }
    [[nodiscard]] std::span<const ParametricRecordEntry>  InspectRecordEntries() const noexcept { return RecordEntries; }
    [[nodiscard]] const ContentRegion&                    InspectContentRegion() const noexcept { return Region; }

    bool RestoreSerializedRecorder(
        const ParametricBaselineSummary&           ImportedBaseline,
        std::span<const ParametricRecordEntry>     ImportedRecords,
        std::span<const std::byte>                 ImportedRegionBytes);

private:
    void RebuildContentRegionForLiveEntries(const NonLinearHistory& History);

    ContentRegion                      Region        ;   // [-] - 64B-aligned 215 MiB payload block
    ParametricBaselineSummary          Baseline      {}; // [-] - Folded parametric baseline accumulator
    std::vector<ParametricRecordEntry> RecordEntries {}; // [-] - Indexed parametric RecordEntry array
};

} // namespace Frontier::Workspaces
