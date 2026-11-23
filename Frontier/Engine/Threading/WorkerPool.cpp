/*==========================================================================================================================================
                                                                WORKER POOL
                                     ------------------------------------------------------------------
                          Pool threads sleep on the work signal; every completion notifies idle waiters for exact
                                                                accounting.
==========================================================================================================================================*/

// 🧩 Pool threads sleep on the work signal; every completion notifies idle waiters for exact accounting.

#include "WorkerPool.h"

#include <algorithm>
#include <utility>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 CONSTRUCTION / TEARDOWN
//------------------------------------------------------------------------------------------------------------------------

WorkerPool::WorkerPool(uint32_t ThreadCount, std::string PoolName)
    : Name(std::move(PoolName))
{
    const uint32_t Count = std::max(1u, ThreadCount);
    Threads.reserve(Count);
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        Threads.emplace_back([this]() { Run(); });
    }
}

WorkerPool::~WorkerPool()
{
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        Stopping = true;
    }
    WorkAvailable.notify_all();
    for (std::thread& Thread : Threads)
    {
        Thread.join();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void WorkerPool::Submit(std::function<void()> Job)
{
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        Queue.push_back(std::move(Job));
    }
    WorkAvailable.notify_one();
}

void WorkerPool::WaitIdle()
{
    std::unique_lock<std::mutex> Lock(Mutex);
    IdleReached.wait(Lock, [this]() { return Queue.empty() && Active == 0; });
}

uint64_t WorkerPool::QueryCompleted() const
{
    std::lock_guard<std::mutex> Lock(Mutex);
    return Completed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void WorkerPool::Run()
{
    for (;;)
    {
        std::function<void()> Job;
        {
            std::unique_lock<std::mutex> Lock(Mutex);
            WorkAvailable.wait(Lock, [this]() { return Stopping || !Queue.empty(); });
            if (Queue.empty())
            {
                return;   // Stopping with a drained queue: the only exit
            }
            Job = std::move(Queue.front());
            Queue.pop_front();
            Active += 1;
        }

        Job();

        {
            std::lock_guard<std::mutex> Lock(Mutex);
            Active    -= 1;
            Completed += 1;
            if (Queue.empty() && Active == 0)
            {
                IdleReached.notify_all();
            }
        }
    }
}

} // namespace Frontier
