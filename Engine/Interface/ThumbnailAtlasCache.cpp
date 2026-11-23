/*============================================================================================================================================
                                                       THUMBNAILATLASCACHE.CPP
============================================================================================================================================*/
// 🧩 Persist baked gallery thumbnail atlases to a writable per-user cache directory so galleries
// read a finished atlas on the next launch instead of rebaking from zero every time.

#include "ThumbnailAtlasCache.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Frontier
{
namespace
{
constexpr char AtlasMagic[8] = {'F', 'R', 'T', 'P', 'A', 'T', 'C', '1'};
constexpr char SlotMagic[8] = {'F', 'R', 'T', 'M', 'A', 'T', 'C', '1'};

template <typename Scalar>
void WriteScalar(std::ofstream &Stream, Scalar Value)
{
    Stream.write(reinterpret_cast<const char *>(&Value), sizeof(Scalar));
}
template <typename Scalar>
bool ReadScalar(std::ifstream &Stream, Scalar &Value)
{
    Stream.read(reinterpret_cast<char *>(&Value), sizeof(Scalar));
    return bool(Stream);
}
void WriteText(std::ofstream &Stream, const std::string &Text)
{
    WriteScalar<uint32_t>(Stream, uint32_t(Text.size()));
    if (!Text.empty()) Stream.write(Text.data(), std::streamsize(Text.size()));
}
bool ReadText(std::ifstream &Stream, std::string &Text, uint32_t MaximumLength = 4096)
{
    uint32_t Length = 0;
    if (!ReadScalar(Stream, Length) || Length > MaximumLength) return false;
    Text.resize(Length);
    if (Length && !Stream.read(Text.data(), std::streamsize(Length))) return false;
    return true;
}
// 📝 Writes to a sibling temp file then renames over the destination, so a process interrupted
// mid-write (crash, kill, power loss) never leaves a half-written, corrupt-but-present cache file
// that a later launch would otherwise trust.
bool AtomicReplace(const std::filesystem::path &File, const std::string &Bytes) noexcept
{
    try
    {
        std::error_code Failure;
        std::filesystem::create_directories(File.parent_path(), Failure);
        const auto Temp = File.parent_path() / (File.filename().string() + ".tmp");
        {
            std::ofstream Stream(Temp, std::ios::binary | std::ios::trunc);
            if (!Stream) return false;
            Stream.write(Bytes.data(), std::streamsize(Bytes.size()));
            if (!Stream) return false;
        }
        std::filesystem::rename(Temp, File, Failure);
        if (Failure) { std::filesystem::remove(Temp, Failure); return false; }
        return true;
    }
    catch (...) { return false; }
}
}

std::filesystem::path QueryFrontierCacheDirectory()
{
    std::filesystem::path Directory;
#ifdef _WIN32
    if (const auto *Local = _wgetenv(L"LOCALAPPDATA")) Directory = std::filesystem::path(Local) / "Frontier/Cache";
#else
    if (const auto *Local = std::getenv("XDG_CACHE_HOME")) Directory = std::filesystem::path(Local) / "Frontier/Cache";
    else if (const auto *Home = std::getenv("HOME")) Directory = std::filesystem::path(Home) / ".cache/Frontier/Cache";
#endif
    if (Directory.empty()) Directory = std::filesystem::temp_directory_path() / "Frontier/Cache";
    return Directory;
}

uint64_t HashBytes(const void *Data, size_t Count, uint64_t Seed) noexcept
{
    uint64_t Digest = Seed;
    const auto *Octets = static_cast<const unsigned char *>(Data);
    for (size_t Index = 0; Index < Count; ++Index) { Digest ^= Octets[Index]; Digest *= 1099511628211ull; }
    return Digest;
}
uint64_t CombineHash(uint64_t Accumulated, uint64_t Next) noexcept
{
    return HashBytes(&Next, sizeof(Next), Accumulated);
}
uint64_t HashText(const std::string &Text, uint64_t Seed) noexcept
{
    return HashBytes(Text.data(), Text.size(), Seed);
}
uint64_t HashFileContents(const std::filesystem::path &Path, uintmax_t MaximumBytes) noexcept
{
    try
    {
        std::error_code Failure;
        const auto Bytes = std::filesystem::file_size(Path, Failure);
        if (Failure || Bytes == 0 || Bytes > MaximumBytes) return 0;
        std::ifstream Input(Path, std::ios::binary);
        if (!Input) return 0;
        std::string Source(size_t(Bytes), '\0');
        if (!Input.read(Source.data(), std::streamsize(Bytes))) return 0;
        const auto Digest = HashBytes(Source.data(), Source.size());
        return Digest ? Digest : 1; // Never return the sentinel 0 for genuine content.
    }
    catch (...) { return 0; }
}

bool LoadThumbnailAtlasCache(const std::filesystem::path &File, uint64_t Digest,
    uint32_t Width, uint32_t Height, ThumbnailAtlasCacheRecord &Output) noexcept
{
    try
    {
        std::ifstream Stream(File, std::ios::binary);
        if (!Stream) return false;
        char Magic[8]{};
        if (!Stream.read(Magic, 8) || std::string_view(Magic, 8) != std::string_view(AtlasMagic, 8)) return false;
        uint64_t FileDigest = 0; uint32_t FileWidth = 0, FileHeight = 0; uint64_t ByteCount = 0;
        if (!ReadScalar(Stream, FileDigest) || !ReadScalar(Stream, FileWidth) || !ReadScalar(Stream, FileHeight) ||
            !ReadScalar(Stream, ByteCount)) return false;
        if (FileDigest != Digest || FileWidth != Width || FileHeight != Height) return false;
        const uint64_t Expected = uint64_t(Width) * Height * 4;
        if (ByteCount != Expected || ByteCount > 256ull * 1024 * 1024) return false;
        std::vector<uint8_t> Pixels(size_t(ByteCount), uint8_t(0));
        if (!Stream.read(reinterpret_cast<char *>(Pixels.data()), std::streamsize(ByteCount))) return false;
        Output.Width = Width; Output.Height = Height; Output.Rgba = std::move(Pixels);
        return true;
    }
    catch (...) { return false; }
}
void SaveThumbnailAtlasCache(const std::filesystem::path &File, uint64_t Digest,
    uint32_t Width, uint32_t Height, std::span<const uint8_t> Rgba) noexcept
{
    try
    {
        if (uint64_t(Rgba.size()) != uint64_t(Width) * Height * 4) return;
        std::string Bytes;
        Bytes.append(AtlasMagic, 8);
        const auto Append = [&](const void *Data, size_t Count) { Bytes.append(reinterpret_cast<const char *>(Data), Count); };
        Append(&Digest, sizeof(Digest));
        Append(&Width, sizeof(Width));
        Append(&Height, sizeof(Height));
        const uint64_t ByteCount = Rgba.size();
        Append(&ByteCount, sizeof(ByteCount));
        Bytes.append(reinterpret_cast<const char *>(Rgba.data()), Rgba.size());
        (void)AtomicReplace(File, Bytes);
    }
    catch (...) {}
}

bool LoadThumbnailAtlasSlotCache(const std::filesystem::path &File, uint64_t FormatDigest,
    uint32_t Width, uint32_t Height, ThumbnailAtlasSlotCacheRecord &Output) noexcept
{
    try
    {
        std::ifstream Stream(File, std::ios::binary);
        if (!Stream) return false;
        char Magic[8]{};
        if (!Stream.read(Magic, 8) || std::string_view(Magic, 8) != std::string_view(SlotMagic, 8)) return false;
        uint64_t FileFormatDigest = 0; uint32_t FileWidth = 0, FileHeight = 0, SlotCount = 0;
        if (!ReadScalar(Stream, FileFormatDigest) || !ReadScalar(Stream, FileWidth) || !ReadScalar(Stream, FileHeight) ||
            !ReadScalar(Stream, SlotCount)) return false;
        if (FileFormatDigest != FormatDigest || FileWidth != Width || FileHeight != Height || SlotCount > 4096) return false;
        std::vector<ThumbnailAtlasSlotCacheEntry> Slots(SlotCount);
        for (auto &Slot : Slots)
        {
            if (!ReadText(Stream, Slot.RelativePath) || !ReadScalar(Stream, Slot.Digest)) return false;
        }
        uint64_t ByteCount = 0;
        if (!ReadScalar(Stream, ByteCount)) return false;
        const uint64_t Expected = uint64_t(Width) * Height * 4;
        if (ByteCount != Expected || ByteCount > 256ull * 1024 * 1024) return false;
        std::vector<uint8_t> Pixels(size_t(ByteCount), uint8_t(0));
        if (!Stream.read(reinterpret_cast<char *>(Pixels.data()), std::streamsize(ByteCount))) return false;
        Output.Width = Width; Output.Height = Height; Output.Rgba = std::move(Pixels); Output.Slots = std::move(Slots);
        return true;
    }
    catch (...) { return false; }
}
void SaveThumbnailAtlasSlotCache(const std::filesystem::path &File, uint64_t FormatDigest,
    uint32_t Width, uint32_t Height, std::span<const uint8_t> Rgba, std::span<const ThumbnailAtlasSlotCacheEntry> Slots) noexcept
{
    try
    {
        if (uint64_t(Rgba.size()) != uint64_t(Width) * Height * 4 || Slots.size() > 4096) return;
        std::string Bytes;
        Bytes.append(SlotMagic, 8);
        const auto Append = [&](const void *Data, size_t Count) { Bytes.append(reinterpret_cast<const char *>(Data), Count); };
        Append(&FormatDigest, sizeof(FormatDigest));
        Append(&Width, sizeof(Width));
        Append(&Height, sizeof(Height));
        const uint32_t SlotCount = uint32_t(Slots.size());
        Append(&SlotCount, sizeof(SlotCount));
        for (const auto &Slot : Slots)
        {
            const uint32_t Length = uint32_t(std::min<size_t>(Slot.RelativePath.size(), 4096));
            Append(&Length, sizeof(Length));
            Bytes.append(Slot.RelativePath.data(), Length);
            Append(&Slot.Digest, sizeof(Slot.Digest));
        }
        const uint64_t ByteCount = Rgba.size();
        Append(&ByteCount, sizeof(ByteCount));
        Bytes.append(reinterpret_cast<const char *>(Rgba.data()), Rgba.size());
        (void)AtomicReplace(File, Bytes);
    }
    catch (...) {}
}

void PruneThumbnailAtlasCacheDirectory(const std::filesystem::path &Directory, size_t KeepMostRecent) noexcept
{
    try
    {
        if (!std::filesystem::is_directory(Directory)) return;
        std::vector<std::filesystem::directory_entry> Files;
        for (const auto &Entry : std::filesystem::directory_iterator(Directory))
            if (Entry.is_regular_file()) Files.push_back(Entry);
        if (Files.size() <= KeepMostRecent) return;
        std::sort(Files.begin(), Files.end(), [](const auto &Left, const auto &Right)
        { return Left.last_write_time() > Right.last_write_time(); });
        for (size_t Index = KeepMostRecent; Index < Files.size(); ++Index)
        {
            std::error_code Failure;
            std::filesystem::remove(Files[Index].path(), Failure);
        }
    }
    catch (...) {}
}
}
