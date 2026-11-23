/*============================================================================================================================================
                                                       THUMBNAILATLASCACHE.H
============================================================================================================================================*/
// 🧩 Persist baked gallery thumbnail atlases to a writable per-user cache directory so galleries
// read a finished atlas on the next launch instead of rebaking from zero every time.

#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                   WRITABLE CACHE DIRECTORY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Mirrors DiagnosticJournal's platform resolution, but under a "Cache" rather than "Logs" leaf and
// XDG_CACHE_HOME rather than XDG_STATE_HOME on Linux: gallery atlases are disposable, re-creatable
// data (safe to delete any time), unlike durable logs or user documents. Falls back to the OS temp
// directory so a read-only install location never blocks baking outright -- a cache miss is always
// a safe, silent degrade to a live rebake, never a hard failure.
[[nodiscard]] std::filesystem::path QueryFrontierCacheDirectory();

//------------------------------------------------------------------------------------------------------------------------
//                                                   CONTENT HASHING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Same FNV-1a 64 construction already used by MaterialLibraryRendering's ComputeMaterialSourceDigest,
// kept here as a shared, reusable primitive instead of a second copy of the algorithm.
[[nodiscard]] uint64_t HashBytes(const void *Data, size_t Count, uint64_t Seed = 14695981039346656037ull) noexcept;
[[nodiscard]] uint64_t CombineHash(uint64_t Accumulated, uint64_t Next) noexcept;
[[nodiscard]] uint64_t HashText(const std::string &Text, uint64_t Seed = 14695981039346656037ull) noexcept;
// 📝 Returns 0 (never a valid digest from HashBytes on non-empty input in practice, but treated purely
// as a sentinel) when the file cannot be read within a bounded size, so a missing/oversized/unreadable
// source file always reads as a cache miss rather than silently matching a stale digest.
[[nodiscard]] uint64_t HashFileContents(const std::filesystem::path &Path, uintmax_t MaximumBytes = 4 * 1024 * 1024) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                   ATLAS CACHE RECORDS
//------------------------------------------------------------------------------------------------------------------------

struct ThumbnailAtlasCacheRecord
{
    uint32_t Width = 0, Height = 0;
    std::vector<uint8_t> Rgba;
};
// 📝 One self-describing file per atlas identity (the caller folds library content + palette/layout
// into Digest). Any magic/version/digest/dimension mismatch is a cache miss, never a thrown failure --
// callers always retain their live bake path.
[[nodiscard]] bool LoadThumbnailAtlasCache(const std::filesystem::path &File, uint64_t Digest,
    uint32_t Width, uint32_t Height, ThumbnailAtlasCacheRecord &Output) noexcept;
void SaveThumbnailAtlasCache(const std::filesystem::path &File, uint64_t Digest,
    uint32_t Width, uint32_t Height, std::span<const uint8_t> Rgba) noexcept;

// 📝 One slot-indexed record per tile in a reusable LRU-style atlas (Material library): the whole
// packed atlas image is cached wholesale, alongside a per-slot path+digest manifest, so a single
// changed/added/removed material invalidates only its own slot on reload, not the entire atlas.
struct ThumbnailAtlasSlotCacheEntry
{
    std::string RelativePath;
    uint64_t Digest = 0;
};
struct ThumbnailAtlasSlotCacheRecord
{
    uint32_t Width = 0, Height = 0;
    std::vector<uint8_t> Rgba;
    std::vector<ThumbnailAtlasSlotCacheEntry> Slots;
};
[[nodiscard]] bool LoadThumbnailAtlasSlotCache(const std::filesystem::path &File, uint64_t FormatDigest,
    uint32_t Width, uint32_t Height, ThumbnailAtlasSlotCacheRecord &Output) noexcept;
void SaveThumbnailAtlasSlotCache(const std::filesystem::path &File, uint64_t FormatDigest,
    uint32_t Width, uint32_t Height, std::span<const uint8_t> Rgba, std::span<const ThumbnailAtlasSlotCacheEntry> Slots) noexcept;

// 📝 Keeps a cache subdirectory bounded by recency (mirrors DiagnosticJournal's log rotation), so
// stale atlases from an old library/palette/build do not accumulate on disk forever.
void PruneThumbnailAtlasCacheDirectory(const std::filesystem::path &Directory, size_t KeepMostRecent = 8) noexcept;
}
