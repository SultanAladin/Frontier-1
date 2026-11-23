/*==========================================================================================================================================
                                                                WORKER POOL
                                     ------------------------------------------------------------------
                            A fixed set of named threads draining a shared job queue with exact idle accounting.
==========================================================================================================================================*/

// 🧩 Worker pool: a fixed set of named threads draining a shared job queue with exact idle accounting.

#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      WORKER POOL
//------------------------------------------------------------------------------------------------------------------------

// The divisions' shared compute muscle (asset cooking, cluster builds, physics substeps fan-out).
// Deliberately simple: a mutex-guarded deque is not the hot path — job granularity is milliseconds,
// and exact completion accounting matters more here than lock-free heroics. Destruction drains
// every queued job before joining, so submitted work is never silently dropped.
class WorkerPool
{
  public:
    explicit WorkerPool(uint32_t ThreadCount, std::string Name = "FrontierWorker");
    ~WorkerPool();

    WorkerPool(const WorkerPool&)            = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void Submit(std::function<void()> Job);
    void WaitIdle();   // Blocks until the queue is empty and every running job has finished

    [[nodiscard]] uint32_t QueryThreadCount() const noexcept { return static_cast<uint32_t>(Threads.size()); }
    [[nodiscard]] uint64_t QueryCompleted() const;           // [jobs] - Finished since construction
    [[nodiscard]] const std::string& QueryName() const noexcept { return Name; }

  private:
    void Run();

    std::string                        Name;
    std::vector<std::thread>           Threads;
    std::deque<std::function<void()>>  Queue;
    mutable std::mutex                 Mutex;
    std::condition_variable            WorkAvailable;
    std::condition_variable            IdleReached;
    uint64_t                           Active    = 0;     // [jobs] - Currently executing
    uint64_t                           Completed = 0;     // [jobs] - Finished
    bool                               Stopping  = false;
};

} // namespace Frontier
