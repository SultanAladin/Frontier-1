/*============================================================================================================================================
                                                            CRITICALSECTION.H
============================================================================================================================================*/
// 🧩 Adaptive mutual exclusion primitive for infrequent OS boundary and container registration regions

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <mutex>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class CriticalSection
{
public:
    CriticalSection() noexcept = default;
    CriticalSection(const CriticalSection&)            = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;

    void Enter() noexcept
    {
        NativeMutex.lock();
    }

    [[nodiscard]] bool TryEnter() noexcept
    {
        return NativeMutex.try_lock();
    }

    void Leave() noexcept
    {
        NativeMutex.unlock();
    }

private:
    std::mutex NativeMutex {}; // [-] - Underlying OS adaptive mutual exclusion primitive
};

class ScopedCriticalGuard
{
public:
    explicit ScopedCriticalGuard(CriticalSection& Section) noexcept
        : BoundSection(Section)
    {
        BoundSection.Enter();
    }

    ~ScopedCriticalGuard() noexcept
    {
        BoundSection.Leave();
    }

    ScopedCriticalGuard(const ScopedCriticalGuard&)            = delete;
    ScopedCriticalGuard& operator=(const ScopedCriticalGuard&) = delete;

private:
    CriticalSection& BoundSection; // [-] - Guarded critical section reference
};

} // namespace Frontier::Threading
