/*============================================================================================================================================
                                                          ATOMICFILEWRITER.CPP
============================================================================================================================================*/
// 🧩 Implementation of crash-safe atomic file writing, binary reading, and root-contained path verification

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Container/AtomicFileWriter.h"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <vector>

#if defined(_WIN32)
    #include <io.h>
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace Frontier::Serialization
{

namespace
{

std::atomic<uint64_t> GlobalTempSequence { 1u }; // [count] - Monotonic counter isolating concurrent temporary files

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 ATOMIC FILE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

bool WriteBytesAtomically(
    const std::filesystem::path& TargetPath,
    std::span<const std::byte>   PayloadBytes,
    std::string&                 OutDiagnostic)
{
    std::error_code FsError{};
    const std::filesystem::path EnclosingDir = TargetPath.parent_path();
    if (!EnclosingDir.empty())
    {
        std::filesystem::create_directories(EnclosingDir, FsError);
        if (FsError)
        {
            OutDiagnostic = "Failed creating destination directory: " + EnclosingDir.string();
            return false;
        }
    }

    const uint64_t SequenceNumber = GlobalTempSequence.fetch_add(1u, std::memory_order_relaxed);
    const std::filesystem::path TempPath = TargetPath.string() + ".frontier_tmp_" + std::to_string(SequenceNumber);

#if defined(_WIN32)
    HANDLE FileHandle = ::CreateFileW(
        TempPath.c_str(),
        GENERIC_WRITE,
        0u,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (FileHandle == INVALID_HANDLE_VALUE)
    {
        OutDiagnostic = "Failed opening temporary file for atomic write: " + TempPath.string();
        return false;
    }

    std::size_t TotalWritten = 0u;
    while (TotalWritten < PayloadBytes.size())
    {
        const DWORD ChunkBytes = static_cast<DWORD>(
            std::min<std::size_t>(PayloadBytes.size() - TotalWritten, 64u * 1024u * 1024u));
        DWORD WrittenNow = 0u;
        if (!::WriteFile(FileHandle, PayloadBytes.data() + TotalWritten, ChunkBytes, &WrittenNow, nullptr) || WrittenNow == 0u)
        {
            ::CloseHandle(FileHandle);
            std::filesystem::remove(TempPath, FsError);
            OutDiagnostic = "Write failure while staging temporary file: " + TempPath.string();
            return false;
        }
        TotalWritten += static_cast<std::size_t>(WrittenNow);
    }

    const bool Flushed = (::FlushFileBuffers(FileHandle) != 0);
    ::CloseHandle(FileHandle);
    if (!Flushed)
    {
        std::filesystem::remove(TempPath, FsError);
        OutDiagnostic = "FlushFileBuffers failed while staging: " + TempPath.string();
        return false;
    }

    if (!::MoveFileExW(TempPath.c_str(), TargetPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(TempPath, FsError);
        OutDiagnostic = "MoveFileExW atomic replacement failed for: " + TargetPath.string();
        return false;
    }
#else
    const int Descriptor = ::open(TempPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (Descriptor < 0)
    {
        OutDiagnostic = "Failed opening temporary file for atomic write: " + TempPath.string();
        return false;
    }

    std::size_t TotalWritten = 0u;
    while (TotalWritten < PayloadBytes.size())
    {
        const ssize_t WrittenNow = ::write(
            Descriptor,
            PayloadBytes.data() + TotalWritten,
            PayloadBytes.size() - TotalWritten);
        if (WrittenNow <= 0)
        {
            ::close(Descriptor);
            std::filesystem::remove(TempPath, FsError);
            OutDiagnostic = "Write failure while staging temporary file: " + TempPath.string();
            return false;
        }
        TotalWritten += static_cast<std::size_t>(WrittenNow);
    }

    const int FlushResult = ::fsync(Descriptor);
    const int CloseResult = ::close(Descriptor);
    if (FlushResult != 0 || CloseResult != 0)
    {
        std::filesystem::remove(TempPath, FsError);
        OutDiagnostic = "fsync/close failed while staging: " + TempPath.string();
        return false;
    }

    if (std::rename(TempPath.c_str(), TargetPath.c_str()) != 0)
    {
        std::filesystem::remove(TempPath, FsError);
        OutDiagnostic = "Atomic rename failed for: " + TargetPath.string();
        return false;
    }
#endif

    OutDiagnostic.clear();
    return true;
}

bool ReadFileBytes(
    const std::filesystem::path& SourcePath,
    std::vector<std::byte>&      OutBytes,
    std::string&                 OutDiagnostic)
{
    std::ifstream InputStream(SourcePath, std::ios::binary | std::ios::ate);
    if (!InputStream.is_open())
    {
        OutDiagnostic = "Failed opening file for reading: " + SourcePath.string();
        return false;
    }

    const std::streamoff FileSize = InputStream.tellg();
    if (FileSize < 0)
    {
        OutDiagnostic = "Failed querying byte length of file: " + SourcePath.string();
        return false;
    }

    OutBytes.resize(static_cast<std::size_t>(FileSize));
    InputStream.seekg(0, std::ios::beg);
    if (FileSize > 0)
    {
        InputStream.read(reinterpret_cast<char*>(OutBytes.data()), FileSize);
        if (!InputStream)
        {
            OutBytes.clear();
            OutDiagnostic = "Truncated read from file: " + SourcePath.string();
            return false;
        }
    }

    OutDiagnostic.clear();
    return true;
}

bool VerifyPathContainedWithinRoot(
    const std::filesystem::path& RootDirectory,
    const std::filesystem::path& CandidateRelativePath,
    std::filesystem::path&       OutResolvedPath,
    std::string&                 OutDiagnostic)
{
    if (CandidateRelativePath.empty() || CandidateRelativePath.is_absolute())
    {
        OutDiagnostic = "Path must be non-empty and relative to root: " + CandidateRelativePath.string();
        return false;
    }

    std::error_code FsError{};
    const std::filesystem::path CanonicalRoot   = std::filesystem::weakly_canonical(RootDirectory, FsError);
    const std::filesystem::path NormalizedFinal = (CanonicalRoot / CandidateRelativePath).lexically_normal();

    auto RootIter   = CanonicalRoot.begin();
    auto TargetIter = NormalizedFinal.begin();
    for (; RootIter != CanonicalRoot.end(); ++RootIter, ++TargetIter)
    {
        if (TargetIter == NormalizedFinal.end() || *RootIter != *TargetIter)
        {
            OutDiagnostic = "Path escapes root containment boundary: " + CandidateRelativePath.string();
            return false;
        }
    }

    OutResolvedPath = NormalizedFinal;
    OutDiagnostic.clear();
    return true;
}

} // namespace Frontier::Serialization
