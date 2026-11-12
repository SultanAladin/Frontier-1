/*============================================================================================================================================
                                                                SPINLOCK.H
============================================================================================================================================*/
// 🧩 Leaf-only exponential-backoff atomic spin lock for short bounded critical regions

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <atomic>
#include <cstdint>
#include <thread>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class alignas(64) SpinLock
{
public:
    constexpr SpinLock() noexcept = default;
    SpinLock(const SpinLock&)            = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    [[nodiscard]] bool TryAcquire() noexcept
    {
        return !OccupiedMarker.load(std::memory_order_relaxed) &&
               !OccupiedMarker.exchange(true, std::memory_order_acquire);
    }

    void Acquire() noexcept
    {
        uint32_t BackoffIterations = 1u;
        while (!TryAcquire())
        {
            for (uint32_t Step = 0u; Step < BackoffIterations; ++Step)
            {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
                _mm_pause();
#elif defined(__aarch64__) || defined(__arm__)
                asm volatile("yield" ::: "memory");
#endif
            }
            if (BackoffIterations < 64u)
            {
                BackoffIterations <<= 1u;
            }
            else
            {
                std::this_thread::yield();
            }
        }
    }

    void Release() noexcept
    {
        OccupiedMarker.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool QueryLocked() const noexcept
    {
        return OccupiedMarker.load(std::memory_order_relaxed);
    }

private:
    std::atomic<bool> OccupiedMarker { false }; // [-] - Cache-line-isolated occupancy indicator
};

class ScopedSpinGuard
{
public:
    explicit ScopedSpinGuard(SpinLock& TargetLock) noexcept
        : BoundLock(TargetLock)
    {
        BoundLock.Acquire();
    }

    ~ScopedSpinGuard() noexcept
    {
        BoundLock.Release();
    }

    ScopedSpinGuard(const ScopedSpinGuard&)            = delete;
    ScopedSpinGuard& operator=(const ScopedSpinGuard&) = delete;

private:
    SpinLock& BoundLock; // [-] - Guarded leaf spin lock reference
};

} // namespace Frontier::Threading
