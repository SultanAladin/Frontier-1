//============================================================================================================================================
//                                                         PLATFORMINTERCHANGE.CPP
//============================================================================================================================================
// 🧩 Process, thread and locale services over the host, behind the layer's one set of conditionals.

#include "SlateMath/Platform/PlatformInterchange/Api/PlatformInterchange.h"

#include <thread>
#include <vector>

// 📝 Every operating-system conditional in the repository lives under `SlateMath/Platform` — `04` §7.
#if defined(_WIN32)
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shlobj.h>

    #if defined(_MSC_VER)
        #pragma comment(lib, "shell32.lib")
        #pragma comment(lib, "ole32.lib")
    #endif
#else
    #include <unistd.h>
    #include <pthread.h>
    #include <sys/stat.h>
    #include <cstdlib>
#endif

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE HOST REPORT
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> PlatformInterchange::Resolve()
{
    if (ReportDelivered)
        return Deliver<bool>::Result(true);

    HostReport Reading;

    // 📝 The resolving count comes from the standard library on every host. It is the one reading no operating
    //    system reports better than the toolchain does, and `04` §7's gate is about conditionals rather than
    //    about which surface answers.
    Reading.ResolvingCount = static_cast<std::uint32_t>(std::thread::hardware_concurrency());

    if (Reading.ResolvingCount == 0u)
        Reading.ResolvingCount = 2u;

    Reading.PhysicalCount = Reading.ResolvingCount;

#if defined(_WIN32)

    SYSTEM_INFO HostInformation = {};
    GetSystemInfo(&HostInformation);

    Reading.ExtentGranule = static_cast<std::uint32_t>(HostInformation.dwAllocationGranularity);

    MEMORYSTATUSEX InstalledExtent = {};
    InstalledExtent.dwLength       = sizeof(InstalledExtent);

    if (GlobalMemoryStatusEx(&InstalledExtent) == FALSE)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the host failed to report its extent" });

    Reading.PhysicalBytes = static_cast<std::uint64_t>(InstalledExtent.ullTotalPhys);

    // 📐 The physical core count is the count of processor records the host reports, which is not the
    //    resolving count wherever the host shares one core between two resolving threads. `34` reserves a
    //    worker for Interactive against the physical count, because two threads sharing one core do not run
    //    the reserved worker and a declared solve at the same time whatever the reservation says.
    DWORD RecordBytes = 0u;
    GetLogicalProcessorInformation(nullptr, &RecordBytes);

    if (RecordBytes != 0u && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        const std::size_t RecordCount = RecordBytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);

        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> Records(RecordCount);

        if (GetLogicalProcessorInformation(Records.data(), &RecordBytes) != FALSE)
        {
            std::uint32_t Counted = 0u;

            for (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION& Record : Records)
            {
                if (Record.Relationship == RelationProcessorCore)
                    ++Counted;
            }

            if (Counted != 0u)
                Reading.PhysicalCount = Counted;
        }
    }

    // 📝 The scale is read from the primary display's own reported density against the unscaled one. `14`
    //    lays its panels out in the artist's units and multiplies by this; a panel laid out in pixels is a
    //    panel that is half the size it should be on the display the artist actually bought.
    const HDC DisplayContext = GetDC(nullptr);

    if (DisplayContext != nullptr)
    {
        const int ReportedDensity = GetDeviceCaps(DisplayContext, LOGPIXELSX);

        ReleaseDC(nullptr, DisplayContext);

        if (ReportedDensity > 0)
            Reading.DisplayScaleMille = static_cast<std::uint32_t>((ReportedDensity * 1000) / 96);
    }

    if (Reading.DisplayScaleMille == 0u)
        Reading.DisplayScaleMille = 1000u;

#else

    const long GranuleBytes = sysconf(_SC_PAGESIZE);
    const long PageCount    = sysconf(_SC_PHYS_PAGES);

    if (GranuleBytes <= 0 || PageCount <= 0)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the host failed to report its extent" });

    Reading.ExtentGranule = static_cast<std::uint32_t>(GranuleBytes);
    Reading.PhysicalBytes = static_cast<std::uint64_t>(GranuleBytes) * static_cast<std::uint64_t>(PageCount);

    // 🚧 `04` §8 leaves which operating systems ship first open. The physical core count and the display scale
    //    are reported through surfaces that differ between the two remaining hosts, and the resolving count
    //    and an unscaled display are the honest readings until one is chosen — not guesses wearing a number.
    Reading.DisplayScaleMille = 1000u;

#endif

    ResolvedReport  = Reading;
    ReportDelivered = true;

    return Deliver<bool>::Result(true);
}

const HostReport& PlatformInterchange::Report() const
{
    return ResolvedReport;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THREAD NAMES
//------------------------------------------------------------------------------------------------------------------------

void PlatformInterchange::DeclareThreadName(const char* ThreadName)
{
    if (ThreadName == nullptr)
        return;

#if defined(_WIN32)

    // 📝 The host takes the name as wide text. The names declared here are short static ASCII spellings, so
    //    the widening is a fixed-extent one on the stack rather than a locale conversion.
    constexpr int NameCapacity = 64;

    wchar_t Widened[NameCapacity] = {};

    const int Widths = MultiByteToWideChar(CP_UTF8, 0, ThreadName, -1, Widened, NameCapacity);

    if (Widths > 0)
        SetThreadDescription(GetCurrentThread(), Widened);

#elif defined(__linux__)

    // 📝 The host truncates at sixteen bytes including the terminator and refuses a longer name outright.
    //    Truncating here is what makes a long name a shortened name rather than no name at all.
    char Bounded[16] = {};

    for (int Index = 0; Index < 15 && ThreadName[Index] != '\0'; ++Index)
        Bounded[Index] = ThreadName[Index];

    pthread_setname_np(pthread_self(), Bounded);

#else

    static_cast<void>(ThreadName);

#endif
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      DIRECTORIES
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::string> PlatformInterchange::ExecutableDirectory()
{
#if defined(_WIN32)

    // 📝 A fixed extent rather than a grown one. The host's own path ceiling is what bounds this, and a path
    //    beyond it names a file the rest of this layer could not open either.
    constexpr DWORD PathCapacity = 32768u;

    std::wstring Located(PathCapacity, L'\0');

    const DWORD Spanned = GetModuleFileNameW(nullptr, Located.data(), PathCapacity);

    if (Spanned == 0u || Spanned >= PathCapacity)
        return Deliver<std::string>::Refuse({ RefusalReason::HostDenied, "the host rejected the executable path" });

    Located.resize(Spanned);

    const std::size_t Separated = Located.find_last_of(L"\\/");

    if (Separated == std::wstring::npos)
        return Deliver<std::string>::Refuse({ RefusalReason::HostDenied, "the executable path names no directory" });

    Located.resize(Separated + 1u);

    const int Narrowed = WideCharToMultiByte(CP_UTF8, 0, Located.c_str(), static_cast<int>(Located.size()),
                                             nullptr, 0, nullptr, nullptr);

    if (Narrowed <= 0)
        return Deliver<std::string>::Refuse({ RefusalReason::HostDenied, "the executable path is not representable" });

    std::string Narrow(static_cast<std::size_t>(Narrowed), '\0');

    WideCharToMultiByte(CP_UTF8, 0, Located.c_str(), static_cast<int>(Located.size()),
                        Narrow.data(), Narrowed, nullptr, nullptr);

    return Deliver<std::string>::Result(Narrow);

#else

    constexpr std::size_t PathCapacity = 4096u;

    std::string Located(PathCapacity, '\0');

    const ssize_t Spanned = readlink("/proc/self/exe", Located.data(), PathCapacity - 1u);

    if (Spanned <= 0)
        return Deliver<std::string>::Refuse({ RefusalReason::HostDenied, "the host rejected the executable path" });

    Located.resize(static_cast<std::size_t>(Spanned));

    const std::size_t Separated = Located.find_last_of('/');

    if (Separated == std::string::npos)
        return Deliver<std::string>::Refuse({ RefusalReason::HostDenied, "the executable path names no directory" });

    Located.resize(Separated + 1u);

    return Deliver<std::string>::Result(Located);

#endif
}

Deliver<std::string> PlatformInterchange::RetainedDirectory(const char* ApplicationName)
{
    if (ApplicationName == nullptr)
        return Deliver<std::string>::Refuse({ RefusalReason::HostDenied, "no application name was declared" });

#if defined(_WIN32)

    PWSTR Located = nullptr;

    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &Located) != S_OK)
    {
        CoTaskMemFree(Located);
        return Deliver<std::string>::Refuse({ RefusalReason::HostDenied, "the host rejected its retained directory" });
    }

    const int Narrowed = WideCharToMultiByte(CP_UTF8, 0, Located, -1, nullptr, 0, nullptr, nullptr);

    if (Narrowed <= 0)
    {
        CoTaskMemFree(Located);
        return Deliver<std::string>::Refuse({ RefusalReason::HostDenied, "the retained path is not representable" });
    }

    std::string Narrow(static_cast<std::size_t>(Narrowed) - 1u, '\0');

    WideCharToMultiByte(CP_UTF8, 0, Located, -1, Narrow.data(), Narrowed, nullptr, nullptr);

    CoTaskMemFree(Located);

    Narrow.push_back('\\');
    Narrow.append(ApplicationName);

    // 📝 Created rather than required. The first run of a freshly installed application is exactly the run
    //    where the directory does not exist yet, and refusing there would make a first run the failing one.
    const int Widths = MultiByteToWideChar(CP_UTF8, 0, Narrow.c_str(), -1, nullptr, 0);

    if (Widths > 0)
    {
        std::wstring Widened(static_cast<std::size_t>(Widths), L'\0');

        MultiByteToWideChar(CP_UTF8, 0, Narrow.c_str(), -1, Widened.data(), Widths);

        if (CreateDirectoryW(Widened.c_str(), nullptr) == FALSE && GetLastError() != ERROR_ALREADY_EXISTS)
            return Deliver<std::string>::Refuse({ RefusalReason::HostDenied, "the retained directory was rejected" });
    }

    Narrow.push_back('\\');

    return Deliver<std::string>::Result(Narrow);

#else

    const char* Retained = std::getenv("XDG_DATA_HOME");
    std::string Located;

    if (Retained != nullptr && Retained[0] != '\0')
    {
        Located.assign(Retained);
    }
    else
    {
        const char* HomeDirectory = std::getenv("HOME");

        if (HomeDirectory == nullptr || HomeDirectory[0] == '\0')
            return Deliver<std::string>::Refuse({ RefusalReason::HostDenied, "the host declared no home directory" });

        Located.assign(HomeDirectory);
        Located.append("/.local/share");
    }

    Located.push_back('/');
    Located.append(ApplicationName);

    if (mkdir(Located.c_str(), 0755) != 0)
    {
        struct stat Existing = {};

        if (stat(Located.c_str(), &Existing) != 0)
            return Deliver<std::string>::Refuse({ RefusalReason::HostDenied, "the retained directory was rejected" });
    }

    Located.push_back('/');

    return Deliver<std::string>::Result(Located);

#endif
}

}   // namespace Slate
