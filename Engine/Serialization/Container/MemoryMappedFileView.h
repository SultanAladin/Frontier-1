/*============================================================================================================================================
                                                          MEMORYMAPPEDFILEVIEW.H
============================================================================================================================================*/
// 🧩 Cross-platform read-only OS memory-mapped file view (Win32 MapViewOfFile / POSIX mmap) enabling zero-copy 64B-aligned section access

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace Frontier::Serialization
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class MemoryMappedFileView
{
public:
    MemoryMappedFileView() noexcept = default;
    ~MemoryMappedFileView() noexcept;

    MemoryMappedFileView(const MemoryMappedFileView&)            = delete;
    MemoryMappedFileView& operator=(const MemoryMappedFileView&) = delete;

    MemoryMappedFileView(MemoryMappedFileView&& Other) noexcept;
    MemoryMappedFileView& operator=(MemoryMappedFileView&& Other) noexcept;

    [[nodiscard]] bool Open(
        const std::filesystem::path& SourcePath,
        std::string&                 OutDiagnostic);

    void Close() noexcept;

    [[nodiscard]] bool QueryOpen() const noexcept;
    [[nodiscard]] std::size_t QueryByteLength() const noexcept;
    [[nodiscard]] std::span<const std::byte> InspectBytes() const noexcept;
    [[nodiscard]] std::span<const std::byte> InspectSlice(std::size_t ByteOffset, std::size_t ByteLength) const noexcept;
    [[nodiscard]] const std::filesystem::path& QuerySourcePath() const noexcept;

private:
    std::filesystem::path SourcePath       {};      // [-] - Opened source file path
    const std::byte*      MappedAddress    = nullptr; // [-] - OS read-only virtual address
    std::size_t           MappedByteLength = 0u;    // [B] - Total mapped file size in bytes
#if defined(_WIN32)
    void*                 FileHandle       = nullptr; // [-] - Win32 HANDLE
    void*                 MappingHandle    = nullptr; // [-] - Win32 file mapping HANDLE
#else
    int                   FileDescriptor   = -1;    // [-] - POSIX file descriptor
#endif
};

} // namespace Frontier::Serialization
