/*============================================================================================================================================
                                                           OPERATIONRECORDER.CPP
============================================================================================================================================*/
// 🧩 Parametric operation recording and Redo-Purge -> Prefix-Fold compaction implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Revision/OperationRecorder.h"

#include <algorithm>
#include <cstring>

namespace Frontier::Workspaces
{

namespace
{

void AssignFixedTag(char (&Destination)[40], std::string_view Source) noexcept
{
    std::memset(Destination, 0, sizeof(Destination));
    const std::size_t Count = std::min<std::size_t>(Source.size(), sizeof(Destination) - 1u);
    if (Count > 0u)
    {
        std::memcpy(Destination, Source.data(), Count);
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 RECORDER OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

OperationRecorder::OperationRecorder(std::size_t MaximumDocumentBudgetBytes)
    : Region(MaximumDocumentBudgetBytes)
{
    Reset();
}

void OperationRecorder::Reset()
{
    Region.Reset();
    Baseline = ParametricBaselineSummary{};
    RecordEntries.clear();

    ParametricRecordEntry AnchorRecord{};
    AnchorRecord.RecordOrdinal       = 0u;
    AnchorRecord.Parameters.Category = ParametricOperationCategory::BaselineAnchor;
    AssignFixedTag(AnchorRecord.Parameters.OperationTag, "BaselineAnchor");
    RecordEntries.push_back(AnchorRecord);
}

void OperationRecorder::ConfigureBudgetCeiling(std::size_t NewBudgetBytes)
{
    Region.ConfigureMaximumBudget(NewBudgetBytes);
}

ParametricOperationParameters OperationRecorder::ConstructNormalizedBrush(
    double           StartU,
    double           StartV,
    double           EndU,
    double           EndV,
    double           NormalizedRadius,
    double           LinearRed,
    double           LinearGreen,
    double           LinearBlue,
    std::string_view Tag) noexcept
{
    ParametricOperationParameters Params{};
    Params.Category   = ParametricOperationCategory::NormalizedBrushSegment;
    Params.Scalars[0] = StartU;
    Params.Scalars[1] = StartV;
    Params.Scalars[2] = EndU;
    Params.Scalars[3] = EndV;
    Params.Scalars[4] = NormalizedRadius;
    Params.Scalars[5] = LinearRed;
    Params.Scalars[6] = LinearGreen;
    Params.Scalars[7] = LinearBlue;
    AssignFixedTag(Params.OperationTag, Tag);
    return Params;
}

ParametricOperationParameters OperationRecorder::ConstructUVTileTransform(
    int32_t          TileU,
    int32_t          TileV,
    double           TranslateU,
    double           TranslateV,
    double           UniformScale,
    double           RotationRadians,
    double           TexelDensityPxPerMeter,
    std::string_view Tag) noexcept
{
    ParametricOperationParameters Params{};
    Params.Category   = ParametricOperationCategory::UVTileTransform;
    Params.TileU      = TileU;
    Params.TileV      = TileV;
    Params.Scalars[0] = TranslateU;
    Params.Scalars[1] = TranslateV;
    Params.Scalars[2] = UniformScale;
    Params.Scalars[3] = RotationRadians;
    Params.Scalars[4] = TexelDensityPxPerMeter;
    AssignFixedTag(Params.OperationTag, Tag);
    return Params;
}

ParametricOperationParameters OperationRecorder::ConstructVectorControlPoint(
    uint32_t         ContourIndex,
    double           NormalizedU,
    double           NormalizedV,
    double           TangentU,
    double           TangentV,
    double           StrokeMillimeters,
    std::string_view Tag) noexcept
{
    ParametricOperationParameters Params{};
    Params.Category      = ParametricOperationCategory::VectorContourControlPoint;
    Params.TargetChannel = ContourIndex;
    Params.Scalars[0]    = NormalizedU;
    Params.Scalars[1]    = NormalizedV;
    Params.Scalars[2]    = TangentU;
    Params.Scalars[3]    = TangentV;
    Params.Scalars[4]    = StrokeMillimeters;
    AssignFixedTag(Params.OperationTag, Tag);
    return Params;
}

bool OperationRecorder::RecordOperation(
    NonLinearHistory&                    History,
    const ParametricOperationParameters& Parameters,
    std::span<const std::byte>           AuxiliaryPayload,
    uint64_t                             GlobalSequenceStamp,
    CompactionReport*                    OutCompaction)
{
    const std::size_t RequiredAlignedPayload =
        AuxiliaryPayload.empty() ? ContentRegion::AlignUp64(sizeof(ParametricOperationParameters))
                                 : ContentRegion::AlignUp64(AuxiliaryPayload.size());
    const std::size_t CeilingBytes = Region.QueryMaximumBudgetBytes();
    if (RequiredAlignedPayload > CeilingBytes)
    {
        return false;
    }

    CompactionReport Report{};
    if (QueryTotalFootprintBytes() + RequiredAlignedPayload > CeilingBytes)
    {
        Report = EnforceBudgetCeiling(History, CeilingBytes - RequiredAlignedPayload);
    }
    if (OutCompaction != nullptr)
    {
        *OutCompaction = Report;
    }

    std::span<const std::byte> BytesToAppend = AuxiliaryPayload;
    const auto                 InlineBytes  = std::as_bytes(std::span<const ParametricOperationParameters>(&Parameters, 1u));
    if (BytesToAppend.empty())
    {
        BytesToAppend = InlineBytes;
    }

    const std::size_t Offset = Region.AppendAlignedPayload(BytesToAppend);
    if (Offset == ContentRegion::InvalidOffset)
    {
        return false;
    }

    const uint32_t        NewRecordOrdinal = static_cast<uint32_t>(RecordEntries.size());
    ParametricRecordEntry Entry{};
    Entry.RecordOrdinal         = NewRecordOrdinal;
    Entry.Parameters            = Parameters;
    Entry.RegionByteOffset      = Offset;
    Entry.RegionByteLength      = BytesToAppend.size();
    Entry.AlignedFootprintBytes = ContentRegion::AlignUp64(BytesToAppend.size());
    RecordEntries.push_back(Entry);

    (void)History.AppendStepFromCursor(NewRecordOrdinal, GlobalSequenceStamp);
    return true;
}

CompactionReport OperationRecorder::PurgeInactiveRedo(NonLinearHistory& History)
{
    CompactionReport Report{};
    Report.BytesBeforeCompaction = QueryTotalFootprintBytes();
    const std::vector<uint32_t> RetiredRecords = History.PurgeInactiveBranches();
    for (uint32_t RecordIndex : RetiredRecords)
    {
        if (RecordIndex < RecordEntries.size())
        {
            RecordEntries[RecordIndex].AbsorbedOrPurged = true;
        }
    }
    Report.PurgedRedoSteps = static_cast<uint32_t>(RetiredRecords.size());
    if (!RetiredRecords.empty())
    {
        RebuildContentRegionForLiveEntries(History);
    }
    Report.BytesAfterCompaction = QueryTotalFootprintBytes();
    return Report;
}

CompactionReport OperationRecorder::FoldOldestPrefix(NonLinearHistory& History, uint32_t StepCount)
{
    CompactionReport Report{};
    Report.BytesBeforeCompaction = QueryTotalFootprintBytes();
    const std::vector<uint32_t> FoldedRecords = History.FoldOldestActivePrefixSteps(StepCount);
    for (uint32_t RecordIndex : FoldedRecords)
    {
        if (RecordIndex < RecordEntries.size())
        {
            ParametricRecordEntry& Entry = RecordEntries[RecordIndex];
            Entry.AbsorbedOrPurged       = true;
            Baseline.FoldedOperations.push_back(Entry.Parameters);
            ++Baseline.FoldedOperationCount;
            if (Entry.Parameters.Category == ParametricOperationCategory::ScalarChannelAdjustment)
            {
                Baseline.CumulativeExposureEV += Entry.Parameters.Scalars[1];
            }
        }
    }
    Report.FoldedPrefixSteps = static_cast<uint32_t>(FoldedRecords.size());
    if (!FoldedRecords.empty())
    {
        RebuildContentRegionForLiveEntries(History);
    }
    Report.BytesAfterCompaction = QueryTotalFootprintBytes();
    return Report;
}

CompactionReport OperationRecorder::EnforceBudgetCeiling(NonLinearHistory& History, std::size_t TargetCeilingBytes)
{
    CompactionReport Combined{};
    Combined.BytesBeforeCompaction = QueryTotalFootprintBytes();
    if (Combined.BytesBeforeCompaction <= TargetCeilingBytes)
    {
        Combined.BytesAfterCompaction = Combined.BytesBeforeCompaction;
        return Combined;
    }

    // Step 1: Redo Purge — evict inactive branches first so active undo history stays intact.
    const CompactionReport PurgeReport = PurgeInactiveRedo(History);
    Combined.PurgedRedoSteps           = PurgeReport.PurgedRedoSteps;

    // Step 2: Prefix Fold — fold oldest active steps into ParametricBaselineSummary until within ceiling.
    while (QueryTotalFootprintBytes() > TargetCeilingBytes && History.QueryUndoAvailable())
    {
        const CompactionReport FoldReport = FoldOldestPrefix(History, 1u);
        if (FoldReport.FoldedPrefixSteps == 0u)
        {
            break;
        }
        Combined.FoldedPrefixSteps += FoldReport.FoldedPrefixSteps;
    }

    Combined.BytesAfterCompaction = QueryTotalFootprintBytes();
    return Combined;
}

std::size_t OperationRecorder::QueryTotalFootprintBytes() const noexcept
{
    return Region.QueryBytesUsed();
}

void OperationRecorder::RebuildContentRegionForLiveEntries(const NonLinearHistory& History)
{
    ContentRegion CompactedRegion(Region.QueryMaximumBudgetBytes());
    std::vector<bool> LiveRecordMask(RecordEntries.size(), false);
    for (const HistoryStepEntry& Step : History.InspectEntries())
    {
        if (!Step.RetiredEntry && Step.RecordEntryIndex < RecordEntries.size())
        {
            LiveRecordMask[Step.RecordEntryIndex] = true;
        }
    }

    for (std::size_t Index = 1u; Index < RecordEntries.size(); ++Index)
    {
        ParametricRecordEntry& Entry = RecordEntries[Index];
        if (!LiveRecordMask[Index] || Entry.AbsorbedOrPurged || Entry.AlignedFootprintBytes == 0u)
        {
            Entry.RegionByteOffset      = 0u;
            Entry.RegionByteLength      = 0u;
            Entry.AlignedFootprintBytes = 0u;
            continue;
        }

        const std::span<const std::byte> PreviousBytes =
            Region.InspectSpan(Entry.RegionByteOffset, Entry.RegionByteLength);
        const std::size_t NewOffset = CompactedRegion.AppendAlignedPayload(PreviousBytes);
        if (NewOffset != ContentRegion::InvalidOffset)
        {
            Entry.RegionByteOffset      = NewOffset;
            Entry.AlignedFootprintBytes = ContentRegion::AlignUp64(Entry.RegionByteLength);
        }
    }

    Region = std::move(CompactedRegion);
}

bool OperationRecorder::RestoreSerializedRecorder(
    const ParametricBaselineSummary&       ImportedBaseline,
    std::span<const ParametricRecordEntry> ImportedRecords,
    std::span<const std::byte>             ImportedRegionBytes)
{
    if (!Region.AssignRawBytes(ImportedRegionBytes))
    {
        return false;
    }
    Baseline = ImportedBaseline;
    RecordEntries.assign(ImportedRecords.begin(), ImportedRecords.end());
    if (RecordEntries.empty())
    {
        ParametricRecordEntry AnchorRecord{};
        AnchorRecord.RecordOrdinal       = 0u;
        AnchorRecord.Parameters.Category = ParametricOperationCategory::BaselineAnchor;
        AssignFixedTag(AnchorRecord.Parameters.OperationTag, "BaselineAnchor");
        RecordEntries.push_back(AnchorRecord);
    }
    return true;
}

} // namespace Frontier::Workspaces
