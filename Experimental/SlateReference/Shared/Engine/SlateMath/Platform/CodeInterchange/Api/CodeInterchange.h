//============================================================================================================================================
//                                                           CODEINTERCHANGE.H
//============================================================================================================================================
// 🧩 Foreign compiled code crossing in — opaque tokens, a verified interface hash, no standard type in a signature.

#pragma once

#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>
#include <string>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE FLAT SURFACE
//------------------------------------------------------------------------------------------------------------------------

// 📝 🔴 Everything between this banner and the next crosses to code compiled by a toolchain that is not this
//    build's. `04` §5 licenses the flat apparatus **here and nowhere else** — the five Slate units are static
//    libraries produced by one invocation with identical switches, and a marshalling layer between them solves a
//    problem that does not exist. `00` §2.1 states it; this file is the only place the licence is taken up.
// 🔴 No standard-library type appears in any declaration below. A `std::string` or a `std::vector` crossing here
//    would be laid out by whichever standard library the foreign code was built against, and two layouts of one
//    name is the failure mode that presents as arbitrary content rather than as a refusal.

extern "C"
{

/// 🧩 The reservation surface the host lends to foreign code, so nothing frees an extent it did not reserve.
/// note  🔴 Foreign code never releases an extent this process reserved, and this process never releases one the
///        foreign code reserved. Two allocators exist — one per toolchain — and an extent returned to the wrong
///        one corrupts a structure neither side can attribute afterwards.
/// tag   guarantee, nonallocating
struct SlateExtentExchange
{
    void*  (*Reserve)(void* Current, std::uint64_t WantedBytes);   // [-] - null when the reservation is rejected
    void   (*Release)(void* Current, void* Reserved);              // [-] - only what this same surface reserved
    void*  Current;                                                // [-] - the reserving side's own context
};

/// 🧩 What a foreign module reports about itself before any of its entry points is taken.
/// note  🔴 `InterfaceHash` is verified **before** `EntryTable` is read, never after. A module built against a
///        different declaration of that table has a table of a different shape, so reading it to find out
///        whether it may be read is the defect the hash exists to prevent.
/// note  ⚠️ Fixed-width members and a fixed order. Adding a member anywhere but the end changes the offsets a
///        module compiled against the previous spelling reads at, and a module that verified its hash and then
///        read the wrong offsets is worse than one that failed to load.
/// tag   guarantee, nonallocating
struct SlateModuleReport
{
    std::uint32_t         InterfaceMajor;   // [-] - incompatible when it differs from the host's
    std::uint32_t         InterfaceMinor;   // [-] - additive; a module may report less than the host carries
    std::uint64_t         InterfaceHash;    // [-] - of the declarations, produced by the build
    const char*           ModuleName;       // [-] - static text owned by the module; never released here
    SlateExtentExchange   Reserving;        // [-] - the host's surface, handed down at acquisition
    const void*           EntryTable;       // [-] - read only after the hash verifies
};

// 📝 The one exported name a foreign module carries. Everything else it offers is reached through the entry
//    table the report names, so the loader resolves exactly one symbol regardless of how large the module is.
typedef const SlateModuleReport* (*SlateAcquireModuleEntry)(std::uint32_t RequestedInterfaceMajor);

}   // extern "C"

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE ACQUISITION
//------------------------------------------------------------------------------------------------------------------------

// 📝 No module; never a valid module ordinal. Sibling of `ShaderCodec`'s `AbsentModule`.
inline constexpr std::uint32_t AbsentForeignModule = 0xFFFFFFFFu;   // [-] - the reference names no module

/// 🧩 What this process requires of a module before it will take an entry point from it.
/// note  📝 The host's own hash is supplied by the caller rather than read from a header here, because the
///        declaration the hash covers is the caller's — `04` §5's apparatus is one mechanism serving whatever
///        interface a consumer declares, not one interface.
/// tag   nonallocating, nonthrowing
struct ForeignRequirement
{
    std::uint32_t  InterfaceMajor = 0u;   // [-] - what this process declares; a differing report is rejected
    std::uint64_t  InterfaceHash  = 0u;   // [-] - what this process declares; a differing report is rejected
};

/// 🧩 The one place genuinely foreign compiled code crosses into this process.
/// note  🔴 `04` §8 records that whether this component has a consumer in scope at all is open. It is built
///        because `04` §7 gates that the flat apparatus appears **only** here — a gate over an absent component
///        is a gate nothing can violate and nothing can be checked against.
/// note  ⚠️ Nothing is released while a module stands. A module unloaded while this process still holds a
///        pointer into its code leaves a call landing in an extent the host reassigned, which reports as a
///        fault at an address belonging to no module at all.
/// tag   owning
class CodeInterchange
{
public:

    static constexpr std::uint32_t ModuleCapacity = 16u;   // [-] - modules held standing at once

    CodeInterchange()                                  = default;
    CodeInterchange(const CodeInterchange&)            = delete;
    CodeInterchange& operator=(const CodeInterchange&) = delete;
    ~CodeInterchange();

    /// 🧩 Loads a foreign module, verifies its report, and holds it standing.
    /// in    ModulePath   [-]  the host path of the compiled module
    /// in    Required     [-]  the interface this process declares and will accept nothing else for
    /// out   Result      [-]  the module ordinal; refuses with HostDenied when the host declines the load or
    ///                         the module exports no acquisition entry, with VersionUnmigratable when the
    ///                         reported major differs, and with ContentUnsupported when the hash differs or the
    ///                         report names no entry table
    /// note  🔴 The major is compared for **equality** and not for "at least". A module reporting a later major
    ///        was compiled against declarations this process has never seen, and accepting it because it is
    ///        newer reads a table whose shape this process is guessing at.
    /// note  ⚠️ The module is unloaded again on every refusing path. A module that loaded, failed verification
    ///        and stayed resident has already run its own initialisation inside this process.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Acquire(const std::string& ModulePath, ForeignRequirement Required);

    /// 🧩 The verified entry table of a standing module.
    /// in    ModuleIndex [-]  a module this component acquired
    /// out   Result       [-]  refuses with IdentityStale for an ordinal no module stands at
    /// note  🔴 Read as an opaque address and given its shape by the caller, which is the only side that knows
    ///        what the verified hash covered. Declaring the shape here would make this component depend on
    ///        every interface any consumer ever loads.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<const void*> EntryTable(std::uint32_t ModuleIndex) const;

    /// 🧩 What a standing module reported about itself.
    /// in    ModuleIndex [-]  a module this component acquired
    /// out   Result       [-]  refuses with IdentityStale for an ordinal no module stands at
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<const SlateModuleReport*> Report(std::uint32_t ModuleIndex) const;

    /// 🧩 Releases one standing module.
    /// in    ModuleIndex [-]  a module this component acquired
    /// pre   nothing this process holds points into the module's code or its extents
    /// cost  🚩
    /// tag   api, nonthrowing
    void Release(std::uint32_t ModuleIndex);

    /// 🧩 Releases every standing module. Called by the destructor as well.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Reclaim();

    /// 🧩 How many modules stand.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t CurrentCount() const;

private:

    struct CurrentModule
    {
        void*                      HostToken = nullptr;   // [-] - opaque; the host spelling stays in the source
        const SlateModuleReport*   Reported  = nullptr;   // [-] - owned by the module, valid while it stands
    };

    CurrentModule  Current[ModuleCapacity] = {};   // [-] - fixed; a seventeenth module is rejected, not grown
};

}   // namespace Slate
