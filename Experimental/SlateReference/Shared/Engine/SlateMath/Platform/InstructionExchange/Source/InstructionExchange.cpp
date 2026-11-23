//============================================================================================================================================
//                                                        INSTRUCTIONEXCHANGE.CPP
//============================================================================================================================================
// 🧩 The host query, the operating-system consent a wide specialisation additionally needs, and the selection.

#include "SlateMath/Platform/InstructionExchange/Api/InstructionExchange.h"

// 📝 Every operating-system conditional in the repository lives under `SlateMath/Platform` — `04` §7. The
//    instruction-set query is additionally architecture-conditional, which is why both spellings appear here and
//    in no other file.
#if defined(_M_X64) || defined(__x86_64__)
    #define SLATE_INSTRUCTION_QUERY 1
#else
    #define SLATE_INSTRUCTION_QUERY 0
#endif

#if SLATE_INSTRUCTION_QUERY
    #if defined(_MSC_VER)
        #include <intrin.h>
        #include <immintrin.h>
    #else
        #include <cpuid.h>
        #include <immintrin.h>
    #endif
#endif

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE HOST QUERY
//------------------------------------------------------------------------------------------------------------------------

namespace
{

#if SLATE_INSTRUCTION_QUERY

void QueryHost(std::uint32_t Leaf, std::uint32_t Subleaf, std::uint32_t Reported[4])
{
#if defined(_MSC_VER)
    int Landing[4] = { 0, 0, 0, 0 };
    __cpuidex(Landing, static_cast<int>(Leaf), static_cast<int>(Subleaf));

    for (std::uint32_t Index = 0u; Index < 4u; ++Index)
        Reported[Index] = static_cast<std::uint32_t>(Landing[Index]);
#else
    __cpuid_count(Leaf, Subleaf, Reported[0], Reported[1], Reported[2], Reported[3]);
#endif
}

// 🔴 The host decoding an instruction is not sufficient. The wide registers are saved and restored by the
//    operating system across a context switch, and an operating system that did not consent to saving them
//    leaves a computation reading whatever the last process left there. The consent is read from the extended
//    control register and is a separate question from what the processor supports.
std::uint64_t ExtendedConsent()
{
#if defined(_MSC_VER)
    return _xgetbv(0);
#else
    std::uint32_t Lower = 0u;
    std::uint32_t Upper = 0u;

    __asm__ __volatile__("xgetbv" : "=a"(Lower), "=d"(Upper) : "c"(0u));

    return (static_cast<std::uint64_t>(Upper) << 32) | static_cast<std::uint64_t>(Lower);
#endif
}

#endif

InstructionWidth ResolveSupported()
{
#if SLATE_INSTRUCTION_QUERY

    std::uint32_t Reported[4] = { 0u, 0u, 0u, 0u };

    QueryHost(0u, 0u, Reported);

    const std::uint32_t MaximumLeaf = Reported[0];

    if (MaximumLeaf < 1u)
        return InstructionWidth::Baseline;

    QueryHost(1u, 0u, Reported);

    // 📝 Bit 27 of the fourth report is the extended-save consent's own presence, and bit 28 is the 256-bit
    //    support. Both are required before the control register may be read at all — reading it without the
    //    first is an illegal instruction rather than a false answer.
    const bool SaveConsentPresent = (Reported[2] & (1u << 27)) != 0u;
    const bool WidenedDecoded     = (Reported[2] & (1u << 28)) != 0u;
    const bool FusedDecoded       = (Reported[2] & (1u << 12)) != 0u;

    if (!SaveConsentPresent || !WidenedDecoded)
        return InstructionWidth::Baseline;

    const std::uint64_t Consented = ExtendedConsent();

    // 📐 Bits 1 and 2 are the lower and upper halves of the 256-bit register file. Both are required: the
    //    operating system consenting to save one half and not the other leaves the upper half undefined
    //    across a switch, which reads as arithmetic that is correct until the scheduler intervenes.
    if ((Consented & 0x6ull) != 0x6ull)
        return InstructionWidth::Baseline;

    if (!FusedDecoded)
        return InstructionWidth::Baseline;

    if (MaximumLeaf < 7u)
        return InstructionWidth::Widened;

    QueryHost(7u, 0u, Reported);

    const bool WidenedIntegerDecoded = (Reported[1] & (1u << 5)) != 0u;
    const bool ExtendedDecoded       = (Reported[1] & (1u << 16)) != 0u;

    if (!WidenedIntegerDecoded)
        return InstructionWidth::Baseline;

    // 📐 Bits 5, 6 and 7 are the opmask register and the two halves of the 512-bit register file. `06`'s
    //    `HardwareMetrics` needs the distinction between a host that decodes the widest specialisation and one
    //    whose operating system agreed to preserve it, because only the second can be measured meaningfully.
    if (ExtendedDecoded && (Consented & 0xE0ull) == 0xE0ull)
        return InstructionWidth::Extended;

    return InstructionWidth::Widened;

#else

    // 📝 A host this build was not specialised for reports the baseline rather than refusing. `02` §7's parity
    //    then compares one specialisation against itself, which is what it already does until `06` brings the
    //    device up — the reporting stays truthful either way.
    return InstructionWidth::Baseline;

#endif
}

std::uint32_t ResolveCacheLine()
{
#if SLATE_INSTRUCTION_QUERY

    std::uint32_t Reported[4] = { 0u, 0u, 0u, 0u };

    QueryHost(0x80000000u, 0u, Reported);

    if (Reported[0] < 0x80000006u)
        return 64u;

    QueryHost(0x80000006u, 0u, Reported);

    const std::uint32_t LineBytes = Reported[2] & 0xFFu;

    // 📝 Sixty-four where the host declines to say. It is the extent every host this build targets actually
    //    carries, and a zero delivered upward would be divided by wherever a stride was derived from it.
    return LineBytes != 0u ? LineBytes : 64u;

#else

    return 64u;

#endif
}

// 🔴 The selection is a single record read once, per `04` §6. `02` §7's sweep moves it deliberately through
//    Fix, which is why it is not const — but nothing else writes it, and a selection derived per call site is
//    what this exists to prevent.
InstructionReport& CurrentReport()
{
    // 📝 Held as a function-local static so the query happens on the first read rather than before the process
    //    has a chance to run at all. Initialisation of a function-local static is thread-safe under C++20, so
    //    two workers reaching it at once still perform the query once.
    static InstructionReport Resolved = []
    {
        InstructionReport Reading;

        Reading.Supported      = ResolveSupported();
        Reading.Selected       = Reading.Supported;
        Reading.CacheLineBytes = ResolveCacheLine();

        return Reading;
    }();

    return Resolved;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SELECTION
//------------------------------------------------------------------------------------------------------------------------

const InstructionReport& InstructionExchange::Report()
{
    return CurrentReport();
}

InstructionWidth InstructionExchange::Fix(InstructionWidth Fixed)
{
    InstructionReport& Current = CurrentReport();

    // 🔴 Reduced to what the host supports rather than rejected. A caller sweeping every specialisation asks for
    //    each in turn without first asking which exist, and taking a computation through an instruction the host
    //    does not decode is not a refusal that can be reported — the process stops at the instruction.
    const std::uint32_t Requested = static_cast<std::uint32_t>(Fixed);
    const std::uint32_t Available = static_cast<std::uint32_t>(Current.Supported);

    Current.Selected        = Requested <= Available ? Fixed : Current.Supported;
    Current.SelectionForced = true;

    return Current.Selected;
}

void InstructionExchange::Release()
{
    InstructionReport& Current = CurrentReport();

    Current.Selected        = Current.Supported;
    Current.SelectionForced = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS NAMED
//------------------------------------------------------------------------------------------------------------------------

const char* InstructionExchange::Naming(InstructionWidth Reported)
{
    switch (Reported)
    {
        case InstructionWidth::Widened:  return "widened";
        case InstructionWidth::Extended: return "extended";
        case InstructionWidth::Baseline: return "baseline";
    }

    // 📝 Reached only by an ordinal no enumerator carries, which the type system already excludes. Named rather
    //    than left to fall off the end, because a report reading past this function is a report that names
    //    nothing and `86` presents what it is given verbatim.
    return "baseline";
}

}   // namespace Slate
