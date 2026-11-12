/*============================================================================================================================================
                                                             REVISIONCODEC.CPP
============================================================================================================================================*/
// 🧩 Binary .Baseline and .Revision section serialization implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"

#include <algorithm>
#include <cstring>

namespace Frontier::Codecs
{

namespace
{

constexpr uint32_t BaselineBinarySignature = 0x4C534246u; // 'FBSL'
constexpr uint32_t RevisionBinarySignature = 0x56455246u; // 'FREV'

template <typename PodType>
void AppendPod(std::vector<std::byte>& Buffer, const PodType& Pod)
{
    const std::size_t Start = Buffer.size();
    Buffer.resize(Start + sizeof(PodType));
    std::memcpy(Buffer.data() + Start, &Pod, sizeof(PodType));
}

template <typename PodType>
[[nodiscard]] bool ReadPod(std::span<const std::byte> Buffer, std::size_t& Cursor, PodType& OutPod) noexcept
{
    if (Cursor > Buffer.size() || sizeof(PodType) > (Buffer.size() - Cursor))
    {
        return false;
    }
    std::memcpy(&OutPod, Buffer.data() + Cursor, sizeof(PodType));
    Cursor += sizeof(PodType);
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              REVISION CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void RevisionEncoder::AttachRevisionSections(
    const Workspaces::RevisionTracker& Tracker,
    SpaceContainerPayload&             InOutPayload)
{
    // Remove any existing .Baseline or .Revision sections so re-saving replaces them cleanly.
    InOutPayload.BinarySections.erase(
        std::remove_if(
            InOutPayload.BinarySections.begin(),
            InOutPayload.BinarySections.end(),
            [](const BinarySectionBlock& Existing)
            {
                return Existing.SubFormatTag == ".Baseline" || Existing.SubFormatTag == ".Revision";
            }),
        InOutPayload.BinarySections.end());

    const Workspaces::OperationRecorder&         Recorder = Tracker.InspectRecorder();
    const Workspaces::ParametricBaselineSummary& Baseline = Recorder.InspectBaseline();
    const Workspaces::NonLinearHistory&          History  = Tracker.InspectHistory();

    // 1. Encode .Baseline binary section.
    BinarySectionBlock BaselineBlock{};
    BaselineBlock.SectionIdentifier = "ParametricBaseline";
    BaselineBlock.SubFormatTag      = ".Baseline";
    AppendPod(BaselineBlock.PayloadBytes, BaselineBinarySignature);
    AppendPod(BaselineBlock.PayloadBytes, Baseline.FoldedOperationCount);
    AppendPod(BaselineBlock.PayloadBytes, Baseline.CumulativeExposureEV);
    const uint32_t FoldedListCount = static_cast<uint32_t>(Baseline.FoldedOperations.size());
    AppendPod(BaselineBlock.PayloadBytes, FoldedListCount);
    for (const Workspaces::ParametricOperationParameters& Op : Baseline.FoldedOperations)
    {
        AppendPod(BaselineBlock.PayloadBytes, Op);
    }
    BaselineBlock.Checksum64 = SpaceContainerEncoder::ComputeFnv1aChecksum64(BaselineBlock.PayloadBytes);
    InOutPayload.BinarySections.push_back(std::move(BaselineBlock));

    // 2. Encode .Revision binary section (HistoryStepEntry array + ParametricRecordEntry array + ContentRegion bytes).
    BinarySectionBlock RevisionBlock{};
    RevisionBlock.SectionIdentifier = "ParametricRevisionTree";
    RevisionBlock.SubFormatTag      = ".Revision";
    AppendPod(RevisionBlock.PayloadBytes, RevisionBinarySignature);
    AppendPod(RevisionBlock.PayloadBytes, History.QueryRootIndex());
    AppendPod(RevisionBlock.PayloadBytes, History.QueryCursorIndex());
    AppendPod(RevisionBlock.PayloadBytes, Tracker.QuerySavedStepIndex());
    AppendPod(RevisionBlock.PayloadBytes, History.QueryNextLocalOrdinal());

    const auto     HistoryEntries = History.InspectEntries();
    const uint32_t HistoryCount   = static_cast<uint32_t>(HistoryEntries.size());
    AppendPod(RevisionBlock.PayloadBytes, HistoryCount);
    for (const Workspaces::HistoryStepEntry& Step : HistoryEntries)
    {
        AppendPod(RevisionBlock.PayloadBytes, Step);
    }

    const auto     Records     = Recorder.InspectRecordEntries();
    const uint32_t RecordCount = static_cast<uint32_t>(Records.size());
    AppendPod(RevisionBlock.PayloadBytes, RecordCount);
    for (const Workspaces::ParametricRecordEntry& Entry : Records)
    {
        AppendPod(RevisionBlock.PayloadBytes, Entry);
    }

    const auto     RegionBytes = Recorder.InspectContentRegion().InspectAllBytes();
    const uint64_t RegionSize  = static_cast<uint64_t>(RegionBytes.size());
    AppendPod(RevisionBlock.PayloadBytes, RegionSize);
    if (!RegionBytes.empty())
    {
        const std::size_t Start = RevisionBlock.PayloadBytes.size();
        RevisionBlock.PayloadBytes.resize(Start + RegionBytes.size());
        std::memcpy(RevisionBlock.PayloadBytes.data() + Start, RegionBytes.data(), RegionBytes.size());
    }
    RevisionBlock.Checksum64 = SpaceContainerEncoder::ComputeFnv1aChecksum64(RevisionBlock.PayloadBytes);
    InOutPayload.BinarySections.push_back(std::move(RevisionBlock));
}

bool RevisionDecoder::ExtractRevisionSections(
    const SpaceContainerPayload& Payload,
    Workspaces::RevisionTracker& OutTracker,
    std::string&                 OutDiagnostic)
{
    const BinarySectionBlock* BaselineSection = nullptr;
    const BinarySectionBlock* RevisionSection = nullptr;
    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SubFormatTag == ".Baseline")
        {
            BaselineSection = &Section;
        }
        else if (Section.SubFormatTag == ".Revision")
        {
            RevisionSection = &Section;
        }
    }

    if (BaselineSection == nullptr && RevisionSection == nullptr)
    {
        OutTracker.Reset();
        return true;
    }
    if (BaselineSection == nullptr || RevisionSection == nullptr)
    {
        OutDiagnostic = "Container must include both .Baseline and .Revision sections when persisting revision history.";
        return false;
    }

    // 1. Decode .Baseline
    Workspaces::ParametricBaselineSummary DecodedBaseline{};
    {
        std::size_t Cursor    = 0u;
        uint32_t    Signature = 0u;
        uint32_t    FoldCount = 0u;
        if (!ReadPod(BaselineSection->PayloadBytes, Cursor, Signature) || Signature != BaselineBinarySignature ||
            !ReadPod(BaselineSection->PayloadBytes, Cursor, DecodedBaseline.FoldedOperationCount) ||
            !ReadPod(BaselineSection->PayloadBytes, Cursor, DecodedBaseline.CumulativeExposureEV) ||
            !ReadPod(BaselineSection->PayloadBytes, Cursor, FoldCount))
        {
            OutDiagnostic = "Corrupted .Baseline binary section header.";
            return false;
        }
        DecodedBaseline.FoldedOperations.resize(FoldCount);
        for (uint32_t Index = 0u; Index < FoldCount; ++Index)
        {
            if (!ReadPod(BaselineSection->PayloadBytes, Cursor, DecodedBaseline.FoldedOperations[Index]))
            {
                OutDiagnostic = "Truncated .Baseline folded parametric operation array.";
                return false;
            }
        }
    }

    // 2. Decode .Revision
    std::size_t Cursor           = 0u;
    uint32_t    Signature        = 0u;
    uint32_t    RootIndex        = 0u;
    uint32_t    CursorIndex      = 0u;
    uint32_t    SavedIndex       = 0u;
    uint32_t    NextLocalOrdinal = 1u;
    uint32_t    HistoryCount     = 0u;
    if (!ReadPod(RevisionSection->PayloadBytes, Cursor, Signature) || Signature != RevisionBinarySignature ||
        !ReadPod(RevisionSection->PayloadBytes, Cursor, RootIndex) ||
        !ReadPod(RevisionSection->PayloadBytes, Cursor, CursorIndex) ||
        !ReadPod(RevisionSection->PayloadBytes, Cursor, SavedIndex) ||
        !ReadPod(RevisionSection->PayloadBytes, Cursor, NextLocalOrdinal) ||
        !ReadPod(RevisionSection->PayloadBytes, Cursor, HistoryCount))
    {
        OutDiagnostic = "Corrupted .Revision binary section header.";
        return false;
    }

    std::vector<Workspaces::HistoryStepEntry> HistoryEntries(HistoryCount);
    for (uint32_t Index = 0u; Index < HistoryCount; ++Index)
    {
        if (!ReadPod(RevisionSection->PayloadBytes, Cursor, HistoryEntries[Index]))
        {
            OutDiagnostic = "Truncated .Revision HistoryStepEntry array.";
            return false;
        }
    }

    uint32_t RecordCount = 0u;
    if (!ReadPod(RevisionSection->PayloadBytes, Cursor, RecordCount))
    {
        OutDiagnostic = "Missing RecordCount in .Revision binary section.";
        return false;
    }

    std::vector<Workspaces::ParametricRecordEntry> Records(RecordCount);
    for (uint32_t Index = 0u; Index < RecordCount; ++Index)
    {
        if (!ReadPod(RevisionSection->PayloadBytes, Cursor, Records[Index]))
        {
            OutDiagnostic = "Truncated .Revision ParametricRecordEntry array.";
            return false;
        }
    }

    uint64_t RegionByteSize = 0ull;
    if (!ReadPod(RevisionSection->PayloadBytes, Cursor, RegionByteSize) ||
        RegionByteSize > (RevisionSection->PayloadBytes.size() - Cursor))
    {
        OutDiagnostic = "Invalid ContentRegion byte length in .Revision section.";
        return false;
    }

    const std::span<const std::byte> RegionSpan(
        RevisionSection->PayloadBytes.data() + Cursor,
        static_cast<std::size_t>(RegionByteSize));

    if (!OutTracker.RestoreSerializedRevision(
            DecodedBaseline,
            Records,
            RegionSpan,
            HistoryEntries,
            RootIndex,
            CursorIndex,
            SavedIndex,
            NextLocalOrdinal))
    {
        OutDiagnostic = "RevisionTracker budget exceeded while restoring .Revision section.";
        return false;
    }

    return true;
}

} // namespace Frontier::Codecs
