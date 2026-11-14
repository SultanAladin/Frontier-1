/*============================================================================================================================================
                                                           ASSETSTREAMTHREAD.CPP
============================================================================================================================================*/
// 🧩 Implementation of dual-path SSD random-access and HDD elevator-coalesced sequential streaming thread

#include "Engine/Threading/Threads/Streaming/AssetStreamThread.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <future>

#if defined(__linux__) && !defined(__ANDROID__)
    #include <sys/stat.h>
    #include <sys/sysmacros.h>
#elif defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <winioctl.h>
#endif

namespace Frontier::Threading
{

namespace
{

[[nodiscard]] bool ReadExactFileSlice(
    const std::filesystem::path& SourcePath,
    uint64_t                     ByteOffset,
    uint32_t                     ByteLength,
    std::vector<uint8_t>&        OutBuffer)
{
    OutBuffer.clear();
    if (ByteLength == 0u)
    {
        return true;
    }

    std::ifstream Stream(SourcePath, std::ios::binary);
    if (!Stream.is_open())
    {
        return false;
    }

    Stream.seekg(0, std::ios::end);
    const std::streamoff TotalSize = Stream.tellg();
    if (TotalSize <= 0 || static_cast<uint64_t>(TotalSize) <= ByteOffset)
    {
        return false;
    }

    const uint64_t AvailableBytes = static_cast<uint64_t>(TotalSize) - ByteOffset;
    const uint32_t SafeLength     = static_cast<uint32_t>(std::min<uint64_t>(ByteLength, AvailableBytes));

    OutBuffer.resize(SafeLength);
    Stream.seekg(static_cast<std::streamoff>(ByteOffset), std::ios::beg);
    Stream.read(reinterpret_cast<char*>(OutBuffer.data()), static_cast<std::streamsize>(SafeLength));
    return Stream.good() || Stream.gcount() == static_cast<std::streamsize>(SafeLength);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            ASSETSTREAMTHREAD IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

AssetStreamThread::~AssetStreamThread() noexcept
{
    Finalize();
}

StorageHardwareTier AssetStreamThread::DetectStorageHardwareTier(const std::filesystem::path& MountTargetPath) noexcept
{
#if defined(__ANDROID__)
    (void)MountTargetPath;
    return StorageHardwareTier::FlashStorageRandomIo;
#elif defined(__linux__)
    std::error_code       IoFailure{};
    std::filesystem::path ResolvedTarget = MountTargetPath.empty() ? std::filesystem::current_path(IoFailure) : MountTargetPath;
    while (!ResolvedTarget.empty() && !std::filesystem::exists(ResolvedTarget, IoFailure))
    {
        ResolvedTarget = ResolvedTarget.parent_path();
    }

    struct stat FileStats{};
    if (!ResolvedTarget.empty() && ::stat(ResolvedTarget.c_str(), &FileStats) == 0)
    {
        const unsigned int MajorId = major(FileStats.st_dev);
        const unsigned int MinorId = minor(FileStats.st_dev);
        const std::filesystem::path PartitionRotational =
            std::filesystem::path("/sys/dev/block") /
            (std::to_string(MajorId) + ":" + std::to_string(MinorId)) /
            "queue/rotational";
        const std::filesystem::path WholeDiskRotational =
            std::filesystem::path("/sys/dev/block") /
            (std::to_string(MajorId) + ":0") /
            "queue/rotational";

        for (const std::filesystem::path& Candidate : { PartitionRotational, WholeDiskRotational })
        {
            std::ifstream RotationalFile(Candidate);
            int           RotationalBit = -1;
            if (RotationalFile.is_open() && (RotationalFile >> RotationalBit))
            {
                return (RotationalBit == 1)
                           ? StorageHardwareTier::RotationalSequentialIo
                           : StorageHardwareTier::FlashStorageRandomIo;
            }
        }
    }
    return StorageHardwareTier::FlashStorageRandomIo;
#elif defined(_WIN32)
    (void)MountTargetPath;
    HANDLE VolumeHandle = ::CreateFileW(
        L"\\\\.\\PhysicalDrive0",
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (VolumeHandle != INVALID_HANDLE_VALUE)
    {
        STORAGE_PROPERTY_QUERY Query{};
        Query.PropertyId = StorageDeviceSeekPenaltyProperty;
        Query.QueryType  = PropertyStandardQuery;
        DEVICE_SEEK_PENALTY_DESCRIPTOR Descriptor{};
        DWORD ReturnedBytes = 0u;
        const BOOL Queried = ::DeviceIoControl(
            VolumeHandle,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &Query,
            sizeof(Query),
            &Descriptor,
            sizeof(Descriptor),
            &ReturnedBytes,
            nullptr);
        ::CloseHandle(VolumeHandle);
        if (Queried && Descriptor.IncursSeekPenalty)
        {
            return StorageHardwareTier::RotationalSequentialIo;
        }
    }
    return StorageHardwareTier::FlashStorageRandomIo;
#else
    (void)MountTargetPath;
    return StorageHardwareTier::FlashStorageRandomIo;
#endif
}

const char* AssetStreamThread::QueryHardwareTierTitle(StorageHardwareTier Tier) noexcept
{
    switch (Tier)
    {
        case StorageHardwareTier::AutoDetect:
            return "Auto-Detect";
        case StorageHardwareTier::FlashStorageRandomIo:
            return "SSD Random I/O (4 KiB Scatter)";
        case StorageHardwareTier::RotationalSequentialIo:
            return "HDD Elevator Sweep (2 MiB Coalesced)";
    }
    return "SSD Random I/O";
}

bool AssetStreamThread::Initialize(
    const std::filesystem::path& MountTargetPath,
    StorageHardwareTier          RequestedTier,
    ThreadProgressionTracker*    Monitor)
{
    if (RunningLatch.load(std::memory_order_acquire))
    {
        return true;
    }

    ProgressionMonitor = Monitor;
    if (ProgressionMonitor != nullptr)
    {
        MonitorSlotIndex = ProgressionMonitor->RegisterThreadSlot("AssetStreamThread");
    }

    ConfigureHardwareTierOverride(RequestedTier, MountTargetPath);
    ShutdownRequested.store(false, std::memory_order_release);
    RunningLatch.store(true, std::memory_order_release);
    BackgroundThread = std::thread([this]() { ExecuteBackgroundLoop(); });
    return true;
}

void AssetStreamThread::Finalize() noexcept
{
    if (!RunningLatch.load(std::memory_order_acquire) && !BackgroundThread.joinable())
    {
        return;
    }

    ShutdownRequested.store(true, std::memory_order_release);
    WakeSignal.notify_all();
    if (BackgroundThread.joinable())
    {
        BackgroundThread.join();
    }
    RunningLatch.store(false, std::memory_order_release);
}

void AssetStreamThread::ConfigureHardwareTierOverride(
    StorageHardwareTier          RequestedTier,
    const std::filesystem::path& MountTargetPath) noexcept
{
    const StorageHardwareTier Resolved =
        (RequestedTier == StorageHardwareTier::AutoDetect)
            ? DetectStorageHardwareTier(MountTargetPath)
            : RequestedTier;
    ActiveTier.store(Resolved, std::memory_order_release);
}

bool AssetStreamThread::EnqueueSliceRead(StreamSliceRequest Request)
{
    if (Request.SourcePath.empty())
    {
        if (Request.CompletionTarget != nullptr)
        {
            Request.CompletionTarget->Decrement();
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> Guard(QueueMutex);
        PendingRequests.push_back(std::move(Request));
    }
    WakeSignal.notify_one();
    return true;
}

void AssetStreamThread::FlushPendingSynchronously()
{
    std::vector<StreamSliceRequest> Drained{};
    {
        std::lock_guard<std::mutex> Guard(QueueMutex);
        Drained.swap(PendingRequests);
    }
    if (Drained.empty())
    {
        return;
    }

    if (ActiveTier.load(std::memory_order_acquire) == StorageHardwareTier::RotationalSequentialIo)
    {
        ProcessRotationalElevatorSweep(Drained);
    }
    else
    {
        ProcessFlashStorageBatch(Drained);
    }
}

AssetStreamReading AssetStreamThread::InspectReading() const noexcept
{
    AssetStreamReading Reading{};
    Reading.ResolvedTier        = ActiveTier.load(std::memory_order_acquire);
    Reading.CompletedSliceCount = CompletedSlices.load(std::memory_order_relaxed);
    Reading.CoalescedSweepCount = IssuedSweeps.load(std::memory_order_relaxed);
    Reading.TotalBytesRead      = CumulativeBytes.load(std::memory_order_relaxed);
    Reading.BackwardSeekCount   = BackwardSeeks.load(std::memory_order_relaxed);
    Reading.ThreadRunning       = RunningLatch.load(std::memory_order_acquire);
    return Reading;
}

StorageHardwareTier AssetStreamThread::QueryResolvedTier() const noexcept
{
    return ActiveTier.load(std::memory_order_acquire);
}

bool AssetStreamThread::QueryRunning() const noexcept
{
    return RunningLatch.load(std::memory_order_acquire);
}

void AssetStreamThread::ExecuteBackgroundLoop()
{
    while (!ShutdownRequested.load(std::memory_order_acquire))
    {
        if (ProgressionMonitor != nullptr && MonitorSlotIndex != 0u)
        {
            ProgressionMonitor->RegisterPulse(MonitorSlotIndex, 0u);
        }

        std::vector<StreamSliceRequest> WorkBatch{};
        {
            std::unique_lock<std::mutex> Lock(QueueMutex);
            WakeSignal.wait_for(Lock, std::chrono::milliseconds(8), [this]() {
                return ShutdownRequested.load(std::memory_order_acquire) || !PendingRequests.empty();
            });
            WorkBatch.swap(PendingRequests);
        }

        if (WorkBatch.empty())
        {
            continue;
        }

        if (ActiveTier.load(std::memory_order_acquire) == StorageHardwareTier::RotationalSequentialIo)
        {
            ProcessRotationalElevatorSweep(WorkBatch);
        }
        else
        {
            ProcessFlashStorageBatch(WorkBatch);
        }
    }

    // Drain any final requests queued prior to Finalize
    FlushPendingSynchronously();
}

void AssetStreamThread::ProcessFlashStorageBatch(std::vector<StreamSliceRequest>& Batch)
{
    // 💡 SSD/NVMe path: order strictly by user viewport priority and read exact slices without read-ahead waste
    std::stable_sort(Batch.begin(), Batch.end(), [](const StreamSliceRequest& Left, const StreamSliceRequest& Right) {
        return Left.PriorityRank < Right.PriorityRank;
    });

    std::size_t Cursor = 0u;
    while (Cursor < Batch.size())
    {
        const std::size_t WindowCount = std::min<std::size_t>(FlashStorageMaxBatchWidth, Batch.size() - Cursor);
        std::vector<std::future<std::pair<std::vector<uint8_t>, bool>>> Futures;
        Futures.reserve(WindowCount);

        for (std::size_t LocalIdx = 0u; LocalIdx < WindowCount; ++LocalIdx)
        {
            const StreamSliceRequest& Req = Batch[Cursor + LocalIdx];
            Futures.push_back(std::async(std::launch::deferred, [&Req]() {
                std::vector<uint8_t> Bytes{};
                const bool           Ok = ReadExactFileSlice(Req.SourcePath, Req.ByteOffset, Req.ByteLength, Bytes);
                return std::make_pair(std::move(Bytes), Ok);
            }));
        }

        for (std::size_t LocalIdx = 0u; LocalIdx < WindowCount; ++LocalIdx)
        {
            auto [Bytes, Ok]          = Futures[LocalIdx].get();
            const std::size_t ByteCnt = Bytes.size();
            StreamSliceRequest& Req   = Batch[Cursor + LocalIdx];

            IssuedSweeps.fetch_add(1ull, std::memory_order_relaxed);
            CompletedSlices.fetch_add(1ull, std::memory_order_relaxed);
            CumulativeBytes.fetch_add(static_cast<uint64_t>(ByteCnt), std::memory_order_relaxed);

            if (Req.NotifySliceReady)
            {
                Req.NotifySliceReady(std::move(Bytes), Ok);
            }
            if (Req.CompletionTarget != nullptr)
            {
                Req.CompletionTarget->Decrement();
            }
        }
        Cursor += WindowCount;
    }
}

void AssetStreamThread::ProcessRotationalElevatorSweep(std::vector<StreamSliceRequest>& Batch)
{
    // 💡 HDD path: C-SCAN monotonic (SourcePath, ByteOffset) elevator sort + 2 MiB contiguous sequential coalescing
    std::stable_sort(Batch.begin(), Batch.end(), [](const StreamSliceRequest& Left, const StreamSliceRequest& Right) {
        if (Left.SourcePath != Right.SourcePath)
        {
            return Left.SourcePath < Right.SourcePath;
        }
        return Left.ByteOffset < Right.ByteOffset;
    });

    std::filesystem::path PreviousFile{};
    uint64_t              PreviousOffset = 0ull;

    std::size_t GroupStart = 0u;
    while (GroupStart < Batch.size())
    {
        const std::filesystem::path& GroupFile   = Batch[GroupStart].SourcePath;
        const uint64_t               SweepBegin  = Batch[GroupStart].ByteOffset;
        uint64_t                     SweepEnd    = SweepBegin + Batch[GroupStart].ByteLength;
        std::size_t                  GroupEnd    = GroupStart + 1u;

        while (GroupEnd < Batch.size() && Batch[GroupEnd].SourcePath == GroupFile)
        {
            const uint64_t CandidateEnd = Batch[GroupEnd].ByteOffset + Batch[GroupEnd].ByteLength;
            if (CandidateEnd - SweepBegin > RotationalCoalesceWindowBytes)
            {
                break;
            }
            SweepEnd = (std::max)(SweepEnd, CandidateEnd);
            ++GroupEnd;
        }

        if (GroupFile == PreviousFile && SweepBegin < PreviousOffset)
        {
            BackwardSeeks.fetch_add(1ull, std::memory_order_relaxed);
        }
        PreviousFile   = GroupFile;
        PreviousOffset = SweepEnd;

        std::vector<uint8_t> CoalescedBytes{};
        const uint32_t       CoalescedLength = static_cast<uint32_t>(SweepEnd - SweepBegin);
        const bool           SweepSucceeded  = ReadExactFileSlice(GroupFile, SweepBegin, CoalescedLength, CoalescedBytes);

        IssuedSweeps.fetch_add(1ull, std::memory_order_relaxed);
        CumulativeBytes.fetch_add(static_cast<uint64_t>(CoalescedBytes.size()), std::memory_order_relaxed);

        for (std::size_t Idx = GroupStart; Idx < GroupEnd; ++Idx)
        {
            StreamSliceRequest&  Req = Batch[Idx];
            std::vector<uint8_t> SliceBytes{};
            bool                 SliceOk = false;

            if (SweepSucceeded && Req.ByteOffset >= SweepBegin)
            {
                const std::size_t RelOffset = static_cast<std::size_t>(Req.ByteOffset - SweepBegin);
                if (RelOffset <= CoalescedBytes.size())
                {
                    const std::size_t CopyLen = std::min<std::size_t>(Req.ByteLength, CoalescedBytes.size() - RelOffset);
                    SliceBytes.assign(
                        CoalescedBytes.begin() + static_cast<std::ptrdiff_t>(RelOffset),
                        CoalescedBytes.begin() + static_cast<std::ptrdiff_t>(RelOffset + CopyLen));
                    SliceOk = (CopyLen > 0u || Req.ByteLength == 0u);
                }
            }

            CompletedSlices.fetch_add(1ull, std::memory_order_relaxed);
            if (Req.NotifySliceReady)
            {
                Req.NotifySliceReady(std::move(SliceBytes), SliceOk);
            }
            if (Req.CompletionTarget != nullptr)
            {
                Req.CompletionTarget->Decrement();
            }
        }

        GroupStart = GroupEnd;
    }
}

} // namespace Frontier::Threading
