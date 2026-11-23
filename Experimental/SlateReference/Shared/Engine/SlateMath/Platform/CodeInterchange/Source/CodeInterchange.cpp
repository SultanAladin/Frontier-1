//============================================================================================================================================
//                                                          CODEINTERCHANGE.CPP
//============================================================================================================================================
// 🧩 The load, the verification performed before any table is read, and the unload taken on every refusing path.

#include "SlateMath/Platform/CodeInterchange/Api/CodeInterchange.h"

#include <string>

// 📝 Every operating-system conditional in the repository lives under `SlateMath/Platform` — `04` §7.
#if defined(_WIN32)
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE HOST EDGE
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 The one exported name. Spelled once here rather than at each call so that the loader and any future
//    diagnostic naming it cannot drift apart.
constexpr const char* AcquisitionEntry = "SlateAcquireModule";

#if defined(_WIN32)

std::wstring Widen(const std::string& Narrow)
{
    if (Narrow.empty())
        return std::wstring();

    const int Widths = MultiByteToWideChar(CP_UTF8, 0, Narrow.c_str(), static_cast<int>(Narrow.size()),
                                           nullptr, 0);

    if (Widths <= 0)
        return std::wstring();

    std::wstring Widened(static_cast<std::size_t>(Widths), L'\0');

    MultiByteToWideChar(CP_UTF8, 0, Narrow.c_str(), static_cast<int>(Narrow.size()),
                        Widened.data(), Widths);

    return Widened;
}

#endif

void* LoadModule(const std::string& ModulePath)
{
#if defined(_WIN32)

    const std::wstring Widened = Widen(ModulePath);

    if (Widened.empty())
        return nullptr;

    // 📝 The module's own directory is added to its search order rather than the process's. A foreign module
    //    that ships its own dependencies beside itself otherwise resolves them against the executable's
    //    directory, which is where a differently versioned copy of the same dependency usually already sits.
    return LoadLibraryExW(Widened.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);

#else

    // 📝 Bound at load rather than lazily, so a module missing a symbol is rejected here instead of faulting at
    //    the first call — which would be attributed to the caller rather than to the module.
    return dlopen(ModulePath.c_str(), RTLD_NOW | RTLD_LOCAL);

#endif
}

void UnloadModule(void* HostToken)
{
    if (HostToken == nullptr)
        return;

#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(HostToken));
#else
    dlclose(HostToken);
#endif
}

SlateAcquireModuleEntry ResolveAcquisition(void* HostToken)
{
    if (HostToken == nullptr)
        return nullptr;

#if defined(_WIN32)

    // 📝 🔴 The host returns its procedure address as a function pointer of one shape, and the conversion to the
    //    shape it actually carries is a reinterpretation the language does not otherwise sanction. It is
    //    performed here, once, in the one file `04` §5 licenses the flat apparatus in.
    FARPROC Resolved = GetProcAddress(static_cast<HMODULE>(HostToken), AcquisitionEntry);

    return reinterpret_cast<SlateAcquireModuleEntry>(reinterpret_cast<void*>(Resolved));

#else

    return reinterpret_cast<SlateAcquireModuleEntry>(dlsym(HostToken, AcquisitionEntry));

#endif
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE ACQUISITION
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> CodeInterchange::Acquire(const std::string& ModulePath, ForeignRequirement Required)
{
    if (ModulePath.empty())
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "a module naming no path" });

    std::uint32_t Vacant = AbsentForeignModule;

    for (std::uint32_t Index = 0u; Index < ModuleCapacity; ++Index)
    {
        if (Current[Index].HostToken == nullptr)
        {
            Vacant = Index;
            break;
        }
    }

    if (Vacant == AbsentForeignModule)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ExtentExhausted, "every module slot is occupied" });
    }

    void* const HostToken = LoadModule(ModulePath);

    if (HostToken == nullptr)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::HostDenied, "the host failed to load the module" });

    const SlateAcquireModuleEntry Acquiring = ResolveAcquisition(HostToken);

    if (Acquiring == nullptr)
    {
        UnloadModule(HostToken);

        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::HostDenied, "the module exports no acquisition entry" });
    }

    const SlateModuleReport* const Reported = Acquiring(Required.InterfaceMajor);

    if (Reported == nullptr)
    {
        UnloadModule(HostToken);

        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::VersionUnmigratable, "the module rejected the requested interface" });
    }

    // 🔴 Equality, not "at least". A module reporting a later major was compiled against declarations this
    //    process has never seen, and accepting it because the number is larger means reading a table whose
    //    shape this process is guessing at — which the hash below cannot save, because it would be a hash of
    //    a different declaration agreeing with itself.
    if (Reported->InterfaceMajor != Required.InterfaceMajor)
    {
        UnloadModule(HostToken);

        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::VersionUnmigratable, "the module reports a different interface major" });
    }

    // 🔴 Verified **before** the entry table is read, per the declaration above. The table's shape is exactly
    //    what the hash covers, so reading it to decide whether it may be read is the defect this prevents.
    if (Reported->InterfaceHash != Required.InterfaceHash)
    {
        UnloadModule(HostToken);

        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "the module reports a different interface hash" });
    }

    if (Reported->EntryTable == nullptr)
    {
        UnloadModule(HostToken);

        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "the module reports no entry table" });
    }

    // ⚠️ A module reserving through a surface it never received releases through it too, and the release lands
    //    in the wrong allocator. Rejected rather than accepted with the members left null, because the failure
    //    would surface as a corrupted reservation structure long after this call returned.
    if (Reported->Reserving.Reserve == nullptr || Reported->Reserving.Release == nullptr)
    {
        UnloadModule(HostToken);

        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "the module reports no extent exchange" });
    }

    Current[Vacant].HostToken = HostToken;
    Current[Vacant].Reported  = Reported;

    return Deliver<std::uint32_t>::Result(Vacant);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

Deliver<const void*> CodeInterchange::EntryTable(std::uint32_t ModuleIndex) const
{
    if (ModuleIndex >= ModuleCapacity || Current[ModuleIndex].HostToken == nullptr)
        return Deliver<const void*>::Refuse({ RefusalReason::IdentityStale, "no module stands at that ordinal" });

    return Deliver<const void*>::Result(Current[ModuleIndex].Reported->EntryTable);
}

Deliver<const SlateModuleReport*> CodeInterchange::Report(std::uint32_t ModuleIndex) const
{
    if (ModuleIndex >= ModuleCapacity || Current[ModuleIndex].HostToken == nullptr)
    {
        return Deliver<const SlateModuleReport*>::Refuse(
            { RefusalReason::IdentityStale, "no module stands at that ordinal" });
    }

    return Deliver<const SlateModuleReport*>::Result(Current[ModuleIndex].Reported);
}

std::uint32_t CodeInterchange::CurrentCount() const
{
    std::uint32_t Counted = 0u;

    for (std::uint32_t Index = 0u; Index < ModuleCapacity; ++Index)
    {
        if (Current[Index].HostToken != nullptr)
            ++Counted;
    }

    return Counted;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void CodeInterchange::Release(std::uint32_t ModuleIndex)
{
    if (ModuleIndex >= ModuleCapacity || Current[ModuleIndex].HostToken == nullptr)
        return;

    // 📝 The report is dropped before the module is unloaded. It points into the module's own read-only extent,
    //    so a reference retained across the unload names an address the host has reassigned.
    Current[ModuleIndex].Reported = nullptr;

    UnloadModule(Current[ModuleIndex].HostToken);

    Current[ModuleIndex].HostToken = nullptr;
}

void CodeInterchange::Reclaim()
{
    for (std::uint32_t Index = 0u; Index < ModuleCapacity; ++Index)
        Release(Index);
}

CodeInterchange::~CodeInterchange()
{
    Reclaim();
}

}   // namespace Slate
