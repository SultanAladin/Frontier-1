/*============================================================================================================================================
                                                            ASSETSTREAMTHREAD.H
============================================================================================================================================*/
// 🧩 Dual-path SSD random-access and HDD elevator-coalesced sequential streaming thread for .Space and ThumbnailSheet pages

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Threading/Sync/CompletionCounter.h"
#include "Engine/Threading/Sync/ThreadProgressionTracker.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class StorageHardwareTier : uint8_t
{
    AutoDetect             = 0, // [-] - Detect underlying OS block device seek penalty at mount path
    FlashStorageRandomIo     = 1, // [-] - NVMe/SSD/UFS flash: 4 KiB direct slice reads, high queue width, zero read-ahead bloat
    RotationalSequentialIo = 2, // [-] - Mechanical HDD: C-SCAN monotonic offset sort + 2 MiB contiguous sweep coalescing
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct StreamSliceRequest
{
    std::filesystem::path                            SourcePath          {};       // [-]   - Target .Space or ThumbnailSheet file
    uint64_t                                         ByteOffset          = 0ull;   // [B]   - 64-byte-aligned start offset in file
    uint32_t                                         ByteLength          = 0u;     // [B]   - Requested slice length in bytes
    uint16_t                                         SheetPageOrdinal    = 0u;     // [idx] - Associated ThumbnailSheet page ordinal
    uint16_t                                         PriorityRank        = 0u;     // [-]   - Lower rank executes first on SSD path
    CompletionCounter*                               CompletionTarget    = nullptr;// [-]   - Optional synchronization counter
    std::function<void(std::vector<uint8_t>&&, bool)> NotifySliceReady   {};       // [-]   - Completion callback receiving slice bytes
};

struct AssetStreamReading
{
    StorageHardwareTier ResolvedTier        = StorageHardwareTier::FlashStorageRandomIo; // [-]     - Active hardware streaming strategy
    uint64_t            CompletedSliceCount = 0ull;                                    // [count] - Individual slice requests fulfilled
    uint64_t            CoalescedSweepCount = 0ull;                                    // [count] - Contiguous file read sweeps issued
    uint64_t            TotalBytesRead      = 0ull;                                    // [B]     - Cumulative bytes read from storage
    uint64_t            BackwardSeekCount   = 0ull;                                    // [count] - Backward offset transitions within sweep
    bool                ThreadRunning       = false;                                   // [-]     - True while background thread is active
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class AssetStreamThread
{
public:
    static constexpr uint32_t RotationalCoalesceWindowBytes = 2u * 1024u * 1024u; // [B] - 2 MiB HDD sequential sweep merge window
    static constexpr uint32_t FlashStorageMaxBatchWidth       = 32u;                // [-] - Parallel SSD batch width per wake

    AssetStreamThread() = default;
    ~AssetStreamThread() noexcept;

    AssetStreamThread(const AssetStreamThread&)            = delete;
    AssetStreamThread& operator=(const AssetStreamThread&) = delete;

    [[nodiscard]] static StorageHardwareTier DetectStorageHardwareTier(const std::filesystem::path& MountTargetPath) noexcept;
    [[nodiscard]] static const char*         QueryHardwareTierTitle(StorageHardwareTier Tier) noexcept;

    [[nodiscard]] bool Initialize(
        const std::filesystem::path& MountTargetPath = {},
        StorageHardwareTier          RequestedTier  = StorageHardwareTier::AutoDetect,
        ThreadProgressionTracker*    Monitor        = nullptr);

    void Finalize() noexcept;

    void ConfigureHardwareTierOverride(StorageHardwareTier RequestedTier, const std::filesystem::path& MountTargetPath = {}) noexcept;

    [[nodiscard]] bool EnqueueSliceRead(StreamSliceRequest Request);
    void               FlushPendingSynchronously();

    [[nodiscard]] AssetStreamReading  InspectReading() const noexcept;
    [[nodiscard]] StorageHardwareTier QueryResolvedTier() const noexcept;
    [[nodiscard]] bool                QueryRunning() const noexcept;

private:
    void ExecuteBackgroundLoop();
    void ProcessFlashStorageBatch(std::vector<StreamSliceRequest>& Batch);
    void ProcessRotationalElevatorSweep(std::vector<StreamSliceRequest>& Batch);

    mutable std::mutex               QueueMutex          {};                                         // [-]     - Guards PendingRequests
    std::condition_variable          WakeSignal          {};                                         // [-]     - Signals thread on new slice
    std::vector<StreamSliceRequest>  PendingRequests     {};                                         // [-]     - Queued slice read requests
    std::thread                      BackgroundThread    {};                                         // [-]     - Dedicated streaming OS thread
    ThreadProgressionTracker*        ProgressionMonitor  = nullptr;                                  // [-]     - Optional progression monitor
    uint32_t                         MonitorSlotIndex    = 0u;                                       // [idx]   - Registered monitor slot
    std::atomic<StorageHardwareTier> ActiveTier          { StorageHardwareTier::FlashStorageRandomIo };// [-]     - Active I/O path
    std::atomic<uint64_t>            CompletedSlices     { 0ull };                                   // [count] - Fulfilled slice count
    std::atomic<uint64_t>            IssuedSweeps        { 0ull };                                   // [count] - Underlying file read ops
    std::atomic<uint64_t>            CumulativeBytes     { 0ull };                                   // [B]     - Cumulative bytes read
    std::atomic<uint64_t>            BackwardSeeks       { 0ull };                                   // [count] - Backward seeks inside sweep
    std::atomic<bool>                RunningLatch        { false };                                  // [-]     - Thread loop liveness latch
    std::atomic<bool>                ShutdownRequested   { false };                                  // [-]     - Cooperative stop signal
};

} // namespace Frontier::Threading
