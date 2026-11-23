//============================================================================================================================================
//                                                           FILEINTERCHANGE.CPP
//============================================================================================================================================
// 🧩 Paths, whole streams and the write-verify-replace sequence over the host file system.

#include "SlateMath/Platform/FileInterchange/Api/FileInterchange.h"

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
    #include <sys/stat.h>
    #include <unistd.h>
    #include <cerrno>
    #include <cstdio>
#endif

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PATH SPELLING
//------------------------------------------------------------------------------------------------------------------------

namespace
{

#if defined(_WIN32)

// 📝 🔴 Paths cross this surface as UTF-8 and are widened here. The narrow host entry points interpret their
//    argument in the process code page, which is not UTF-8 on most hosts, so a path spelled in any script but
//    the artist's own would name a different file — or no file — with no error to read.
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

// 📐 The host reports its revision stamp as hundred-nanosecond intervals from its own epoch. It is carried
//    here in nanoseconds so that a stamp read on one host and compared on another compares in one unit —
//    the epochs still differ, which is why this is only ever compared against another stamp from this
//    surface and never against a `TickPoint`.
std::uint64_t ProjectRevision(const FILETIME& Reported)
{
    const std::uint64_t Intervals = (static_cast<std::uint64_t>(Reported.dwHighDateTime) << 32)
                                  |  static_cast<std::uint64_t>(Reported.dwLowDateTime);

    return Intervals * 100ull;
}

#endif

// 📝 The staged stream sits beside the target and carries the target's own name. Placing it in the host's
//    temporary directory would put it on another volume on most installations, and the rename that replaces
//    the target stops being atomic the moment it crosses one.
std::string StagedPath(const std::string& Path)
{
    return Path + ".staged";
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT A PATH NAMES
//------------------------------------------------------------------------------------------------------------------------

Deliver<PathReport> FileInterchange::Resolve(const std::string& Path)
{
    if (Path.empty())
        return Deliver<PathReport>::Refuse({ RefusalReason::HostDenied, "an empty path names nothing" });

    PathReport Reading;

#if defined(_WIN32)

    const std::wstring Widened = Widen(Path);

    if (Widened.empty())
        return Deliver<PathReport>::Refuse({ RefusalReason::HostDenied, "the path is not representable" });

    WIN32_FILE_ATTRIBUTE_DATA Reported = {};

    if (GetFileAttributesExW(Widened.c_str(), GetFileExInfoStandard, &Reported) == FALSE)
    {
        const DWORD Rejected = GetLastError();

        // 📝 Absent is delivered, not rejected. Every other reason the host declines is a refusal, because the
        //    caller asked whether a file is there and got no answer rather than the answer "no".
        if (Rejected == ERROR_FILE_NOT_FOUND || Rejected == ERROR_PATH_NOT_FOUND)
            return Deliver<PathReport>::Result(Reading);

        return Deliver<PathReport>::Refuse({ RefusalReason::HostDenied, "the file system rejected the path" });
    }

    Reading.Revised = ProjectRevision(Reported.ftLastWriteTime);

    if ((Reported.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u)
    {
        Reading.Content = PathContent::Directory;
        return Deliver<PathReport>::Result(Reading);
    }

    // 📝 A reparse point is reported as Foreign rather than followed. Following one is a decision about trust
    //    that `Layer0_Platform` has no standing to make, and `10`'s intake is where a document's source history
    //    is decided.
    if ((Reported.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u)
    {
        Reading.Content = PathContent::Foreign;
        return Deliver<PathReport>::Result(Reading);
    }

    Reading.Content      = PathContent::Stream;
    Reading.SpannedBytes = (static_cast<std::uint64_t>(Reported.nFileSizeHigh) << 32)
                         |  static_cast<std::uint64_t>(Reported.nFileSizeLow);

#else

    struct stat Reported = {};

    if (lstat(Path.c_str(), &Reported) != 0)
        return Deliver<PathReport>::Result(Reading);

    Reading.Revised = static_cast<std::uint64_t>(Reported.st_mtime) * 1000000000ull;

    if (S_ISDIR(Reported.st_mode))
    {
        Reading.Content = PathContent::Directory;
    }
    else if (S_ISREG(Reported.st_mode))
    {
        Reading.Content      = PathContent::Stream;
        Reading.SpannedBytes = static_cast<std::uint64_t>(Reported.st_size);
    }
    else
    {
        Reading.Content = PathContent::Foreign;
    }

#endif

    return Deliver<PathReport>::Result(Reading);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE READ
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::vector<std::uint8_t>> FileInterchange::ReadStream(const std::string& Path)
{
    using StreamDelivery = Deliver<std::vector<std::uint8_t>>;

    const Deliver<PathReport> Located = Resolve(Path);

    if (!Located.Resolved)
        return StreamDelivery::Refuse(Located.Error);

    const PathReport& Reading = Located.Resolve();

    if (Reading.Content != PathContent::Stream)
        return StreamDelivery::Refuse({ RefusalReason::HostDenied, "the path names no readable stream" });

    if (Reading.SpannedBytes > StreamLimit)
        return StreamDelivery::Refuse({ RefusalReason::ExtentExhausted, "the stream spans beyond the read ceiling" });

    std::vector<std::uint8_t> Content(static_cast<std::size_t>(Reading.SpannedBytes));

    if (Reading.SpannedBytes == 0u)
        return StreamDelivery::Result(Content);

#if defined(_WIN32)

    const std::wstring Widened = Widen(Path);

    const HANDLE Stream = CreateFileW(Widened.c_str(),
                                      GENERIC_READ,
                                      FILE_SHARE_READ,
                                      nullptr,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL,
                                      nullptr);

    if (Stream == INVALID_HANDLE_VALUE)
        return StreamDelivery::Refuse({ RefusalReason::HostDenied, "the stream could not be opened for reading" });

    // 🔴 The host reads at most a 32-bit count per call and is entitled to deliver fewer bytes than asked for
    //    on any one of them. Reading in a loop until the whole extent has landed is what makes this a whole
    //    read; one call and a length check would refuse a perfectly good large stream on a loaded host.
    std::uint64_t Read = 0u;

    while (Read < Reading.SpannedBytes)
    {
        const std::uint64_t Remaining = Reading.SpannedBytes - Read;
        const DWORD         Asked     = Remaining > 0x40000000ull ? 0x40000000u
                                                                  : static_cast<DWORD>(Remaining);

        DWORD Delivered = 0u;

        if (ReadFile(Stream, Content.data() + Read, Asked, &Delivered, nullptr) == FALSE || Delivered == 0u)
        {
            CloseHandle(Stream);
            return StreamDelivery::Refuse({ RefusalReason::HostDenied, "the stream was truncated while reading" });
        }

        Read += Delivered;
    }

    CloseHandle(Stream);

#else

    std::FILE* Stream = std::fopen(Path.c_str(), "rb");

    if (Stream == nullptr)
        return StreamDelivery::Refuse({ RefusalReason::HostDenied, "the stream could not be opened for reading" });

    const std::size_t Read = std::fread(Content.data(), 1u, Content.size(), Stream);

    std::fclose(Stream);

    if (Read != Content.size())
        return StreamDelivery::Refuse({ RefusalReason::HostDenied, "the stream was truncated while reading" });

#endif

    return StreamDelivery::Result(Content);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              WRITE, VERIFY, THEN REPLACE
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> FileInterchange::WriteStream(const std::string& Path, const std::vector<std::uint8_t>& Content)
{
    if (Path.empty())
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "an empty path names nothing" });

    const std::string Staged = StagedPath(Path);

    // ① Write the content beside the target, under a name nothing else reads.
#if defined(_WIN32)

    const std::wstring WidenedStaged = Widen(Staged);

    if (WidenedStaged.empty())
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the path is not representable" });

    const HANDLE Stream = CreateFileW(WidenedStaged.c_str(),
                                      GENERIC_WRITE,
                                      0u,
                                      nullptr,
                                      CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL,
                                      nullptr);

    if (Stream == INVALID_HANDLE_VALUE)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the staged stream could not be opened" });

    std::uint64_t Written = 0u;

    while (Written < Content.size())
    {
        const std::uint64_t Remaining = Content.size() - Written;
        const DWORD         Asked     = Remaining > 0x40000000ull ? 0x40000000u
                                                                  : static_cast<DWORD>(Remaining);

        DWORD Read = 0u;

        if (WriteFile(Stream, Content.data() + Written, Asked, &Read, nullptr) == FALSE || Read == 0u)
        {
            CloseHandle(Stream);
            Discard(Reclaim(Staged));
            return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the staged stream rejected the write" });
        }

        Written += Read;
    }

    // 🔴 Flushed before the read-back. Without it the verification reads the host's own write cache and
    //    confirms what was written rather than what landed, which is precisely the failure the sequence
    //    exists to catch — a full volume reports success on the write and produces a short file.
    FlushFileBuffers(Stream);
    CloseHandle(Stream);

#else

    std::FILE* Stream = std::fopen(Staged.c_str(), "wb");

    if (Stream == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the staged stream could not be opened" });

    const std::size_t Read = Content.empty()
                             ? 0u
                             : std::fwrite(Content.data(), 1u, Content.size(), Stream);

    std::fflush(Stream);
    std::fclose(Stream);

    if (Read != Content.size())
    {
        Discard(Reclaim(Staged));
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the staged stream rejected the write" });
    }

#endif

    // ② Read back what landed and compare it against what was written.
    const Deliver<std::vector<std::uint8_t>> Verified = ReadStream(Staged);

    if (!Verified.Resolved)
    {
        Discard(Reclaim(Staged));
        return Deliver<bool>::Refuse(Verified.Error);
    }

    if (Verified.Resolve() != Content)
    {
        Discard(Reclaim(Staged));
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted,
                                       "what landed differs from what was written; the original stands" });
    }

    // ③ Replace the target, and only now. Everything above this line left the original untouched.
#if defined(_WIN32)

    const std::wstring WidenedTarget = Widen(Path);

    if (MoveFileExW(WidenedStaged.c_str(), WidenedTarget.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
    {
        Discard(Reclaim(Staged));
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the verified stream could not replace the target" });
    }

#else

    if (std::rename(Staged.c_str(), Path.c_str()) != 0)
    {
        Discard(Reclaim(Staged));
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the verified stream could not replace the target" });
    }

#endif

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DIRECTORIES
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> FileInterchange::DeclareDirectory(const std::string& Path)
{
    if (Path.empty())
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "an empty path names nothing" });

    // 📝 Every absent directory above the leaf is created in turn. Creating only the leaf refuses on the first
    //    run of an installation whose retained directory does not exist yet, which is the run that matters.
    for (std::size_t Index = 0u; Index <= Path.size(); ++Index)
    {
        const bool Separated = Index == Path.size()
                            || Path[Index] == '\\'
                            || Path[Index] == '/';

        if (!Separated || Index == 0u)
            continue;

        const std::string Leading = Path.substr(0u, Index);

        const Deliver<PathReport> Located = Resolve(Leading);

        if (Located.Resolved && Located.Resolve().Content == PathContent::Directory)
            continue;

        if (Located.Resolved && Located.Resolve().Content != PathContent::Absent)
            return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "a stream stands where a directory is wanted" });

#if defined(_WIN32)

        const std::wstring Widened = Widen(Leading);

        if (CreateDirectoryW(Widened.c_str(), nullptr) == FALSE && GetLastError() != ERROR_ALREADY_EXISTS)
            return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the file system rejected the directory" });

#else

        if (mkdir(Leading.c_str(), 0755) != 0)
        {
            struct stat Existing = {};

            if (stat(Leading.c_str(), &Existing) != 0 || !S_ISDIR(Existing.st_mode))
                return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the file system rejected the directory" });
        }

#endif
    }

    return Deliver<bool>::Result(true);
}

Deliver<bool> FileInterchange::Reclaim(const std::string& Path)
{
    if (Path.empty())
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "an empty path names nothing" });

#if defined(_WIN32)

    const std::wstring Widened = Widen(Path);

    if (DeleteFileW(Widened.c_str()) == FALSE)
    {
        const DWORD Rejected = GetLastError();

        // 📝 An absent path is delivered. The caller asked for the stream not to be there, and it is not.
        if (Rejected != ERROR_FILE_NOT_FOUND && Rejected != ERROR_PATH_NOT_FOUND)
            return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the file system rejected the removal" });
    }

#else

    if (unlink(Path.c_str()) != 0 && errno != ENOENT)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the file system rejected the removal" });

#endif

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PATH ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

std::string FileInterchange::Append(const std::string& Leading, const std::string& Trailing)
{
    if (Leading.empty())
        return Trailing;

    if (Trailing.empty())
        return Leading;

#if defined(_WIN32)
    constexpr char Separator = '\\';
#else
    constexpr char Separator = '/';
#endif

    std::string Assembled = Leading;

    const bool LeadingSeparated  = Assembled.back()  == '\\' || Assembled.back()  == '/';
    const bool TrailingSeparated = Trailing.front()  == '\\' || Trailing.front()  == '/';

    if (!LeadingSeparated && !TrailingSeparated)
    {
        Assembled.push_back(Separator);
        Assembled.append(Trailing);
    }
    else if (LeadingSeparated && TrailingSeparated)
    {
        // 📝 Exactly one separator. Two produce a path most file systems accept and one — the one carrying a
        //    network share — reads as a different root entirely.
        Assembled.append(Trailing, 1u, Trailing.size() - 1u);
    }
    else
    {
        Assembled.append(Trailing);
    }

    return Assembled;
}

}   // namespace Slate
