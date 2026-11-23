/*============================================================================================================================================
                                                         MEMORYMAPPEDFILEVIEW.CPP
============================================================================================================================================*/
// 🧩 Implementation of Win32 MapViewOfFile and POSIX mmap read-only zero-copy file views

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Container/MemoryMappedFileView.h"

#include <utility>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <cerrno>
    #include <cstring>
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace Frontier::Serialization
{

//------------------------------------------------------------------------------------------------------------------------
//                                                RAII LIFECYCLE & MOVES
//------------------------------------------------------------------------------------------------------------------------

MemoryMappedFileView::~MemoryMappedFileView() noexcept
{
    Close();
}

MemoryMappedFileView::MemoryMappedFileView(MemoryMappedFileView&& Other) noexcept
    : SourcePath(std::move(Other.SourcePath)),
      MappedAddress(std::exchange(Other.MappedAddress, nullptr)),
      MappedByteLength(std::exchange(Other.MappedByteLength, 0u)),
#if defined(_WIN32)
      FileHandle(std::exchange(Other.FileHandle, nullptr)),
      MappingHandle(std::exchange(Other.MappingHandle, nullptr))
#else
      FileDescriptor(std::exchange(Other.FileDescriptor, -1))
#endif
{
}

MemoryMappedFileView& MemoryMappedFileView::operator=(MemoryMappedFileView&& Other) noexcept
{
    if (this != &Other)
    {
        Close();
        SourcePath       = std::move(Other.SourcePath);
        MappedAddress    = std::exchange(Other.MappedAddress, nullptr);
        MappedByteLength = std::exchange(Other.MappedByteLength, 0u);
#if defined(_WIN32)
        FileHandle       = std::exchange(Other.FileHandle, nullptr);
        MappingHandle    = std::exchange(Other.MappingHandle, nullptr);
#else
        FileDescriptor   = std::exchange(Other.FileDescriptor, -1);
#endif
    }
    return *this;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  OPEN / CLOSE / SLICE
//------------------------------------------------------------------------------------------------------------------------

bool MemoryMappedFileView::Open(
    const std::filesystem::path& TargetPath,
    std::string&                 OutDiagnostic)
{
    Close();

#if defined(_WIN32)
    const HANDLE OpenedFile = ::CreateFileW(
        TargetPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
        nullptr);
    if (OpenedFile == INVALID_HANDLE_VALUE)
    {
        OutDiagnostic = "Win32 CreateFileW failed for memory-mapped read: " + TargetPath.string();
        return false;
    }

    LARGE_INTEGER SizeReading{};
    if (!::GetFileSizeEx(OpenedFile, &SizeReading) || SizeReading.QuadPart <= 0)
    {
        ::CloseHandle(OpenedFile);
        OutDiagnostic = "Cannot memory-map empty or unreadable file: " + TargetPath.string();
        return false;
    }

    const HANDLE OpenedMapping = ::CreateFileMappingW(
        OpenedFile,
        nullptr,
        PAGE_READONLY,
        0u,
        0u,
        nullptr);
    if (OpenedMapping == nullptr)
    {
        ::CloseHandle(OpenedFile);
        OutDiagnostic = "Win32 CreateFileMappingW failed: " + TargetPath.string();
        return false;
    }

    void* ViewPtr = ::MapViewOfFile(OpenedMapping, FILE_MAP_READ, 0u, 0u, 0u);
    if (ViewPtr == nullptr)
    {
        ::CloseHandle(OpenedMapping);
        ::CloseHandle(OpenedFile);
        OutDiagnostic = "Win32 MapViewOfFile failed: " + TargetPath.string();
        return false;
    }

    SourcePath       = TargetPath;
    FileHandle       = OpenedFile;
    MappingHandle    = OpenedMapping;
    MappedAddress    = reinterpret_cast<const std::byte*>(ViewPtr);
    MappedByteLength = static_cast<std::size_t>(SizeReading.QuadPart);
    OutDiagnostic.clear();
    return true;
#else
    const int Fd = ::open(TargetPath.c_str(), O_RDONLY);
    if (Fd < 0)
    {
        OutDiagnostic = "POSIX open failed for memory-mapped read (" + std::string(std::strerror(errno)) + "): " + TargetPath.string();
        return false;
    }

    struct stat StatBuf{};
    if (::fstat(Fd, &StatBuf) != 0 || StatBuf.st_size <= 0)
    {
        ::close(Fd);
        OutDiagnostic = "Cannot memory-map empty or unreadable file: " + TargetPath.string();
        return false;
    }

    const std::size_t FileLength = static_cast<std::size_t>(StatBuf.st_size);
    void* ViewPtr = ::mmap(nullptr, FileLength, PROT_READ, MAP_PRIVATE, Fd, 0);
    if (ViewPtr == MAP_FAILED)
    {
        ::close(Fd);
        OutDiagnostic = "POSIX mmap failed (" + std::string(std::strerror(errno)) + "): " + TargetPath.string();
        return false;
    }

    SourcePath       = TargetPath;
    FileDescriptor   = Fd;
    MappedAddress    = reinterpret_cast<const std::byte*>(ViewPtr);
    MappedByteLength = FileLength;
    OutDiagnostic.clear();
    return true;
#endif
}

void MemoryMappedFileView::Close() noexcept
{
#if defined(_WIN32)
    if (MappedAddress != nullptr)
    {
        ::UnmapViewOfFile(MappedAddress);
        MappedAddress = nullptr;
    }
    if (MappingHandle != nullptr)
    {
        ::CloseHandle(MappingHandle);
        MappingHandle = nullptr;
    }
    if (FileHandle != nullptr)
    {
        ::CloseHandle(FileHandle);
        FileHandle = nullptr;
    }
#else
    if (MappedAddress != nullptr && MappedByteLength > 0u)
    {
        ::munmap(const_cast<std::byte*>(MappedAddress), MappedByteLength);
        MappedAddress = nullptr;
    }
    if (FileDescriptor >= 0)
    {
        ::close(FileDescriptor);
        FileDescriptor = -1;
    }
#endif
    MappedByteLength = 0u;
    SourcePath.clear();
}

bool MemoryMappedFileView::QueryOpen() const noexcept
{
    return MappedAddress != nullptr && MappedByteLength > 0u;
}

std::size_t MemoryMappedFileView::QueryByteLength() const noexcept
{
    return MappedByteLength;
}

std::span<const std::byte> MemoryMappedFileView::InspectBytes() const noexcept
{
    if (!QueryOpen())
    {
        return {};
    }
    return std::span<const std::byte>(MappedAddress, MappedByteLength);
}

std::span<const std::byte> MemoryMappedFileView::InspectSlice(std::size_t ByteOffset, std::size_t ByteLength) const noexcept
{
    if (!QueryOpen() || ByteOffset > MappedByteLength || ByteLength > (MappedByteLength - ByteOffset))
    {
        return {};
    }
    return std::span<const std::byte>(MappedAddress + ByteOffset, ByteLength);
}

const std::filesystem::path& MemoryMappedFileView::QuerySourcePath() const noexcept
{
    return SourcePath;
}

} // namespace Frontier::Serialization
