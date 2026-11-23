/*==========================================================================================================================================
                                                              CONTENTDIRECTORY.H
==========================================================================================================================================*/
// 🧩 Locate packaged engine content independently of the working directory.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                   DIRECTORY RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

inline std::string EncodeContentPath(const std::filesystem::path &Path)
{
    const auto UTF8 = Path.u8string();
    return std::string(reinterpret_cast<const char *>(UTF8.data()), UTF8.size());
}

inline std::filesystem::path DecodeContentPath(std::string_view Path)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(Path.data()), Path.size()));
}

std::filesystem::path ResolveContentDirectory(const std::filesystem::path &Requested);
//------------------------------------------------------------------------------------------------------------------------
//                                                   DIRECTORY CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

enum class ContentCategory : uint32_t
{
    All,
    Directory,
    Typeface,
    VectorImage,
    RasterImage,
    Descriptor,
    Other
};

struct ContentEntry
{
    std::filesystem::path Path;
    std::string           Name;
    ContentCategory       Category       = ContentCategory::Other;
    uintmax_t             ByteCount      = 0;
    bool                  ByteCountKnown = false;
};

// Read-only, non-recursive enumeration. Links are omitted; navigation is confined to the canonical root.
// This is not a hostile-filesystem security boundary: concurrent filesystem replacement is not locked.
class ContentDirectory
{
  public:
    static constexpr size_t                        EntryLimit = 8192;
    bool                                           ConfigureRoot(const std::filesystem::path &Requested);
    bool                                           Navigate(const std::filesystem::path &Requested);
    bool                                           NavigateUp();
    bool                                           NavigateBack();
    bool                                           NavigateForward();
    bool                                           Refresh();
    bool                                           Select(const std::filesystem::path &Requested);
    void                                           ConfigureFilter(ContentCategory Category, std::string Search);
    void                                           ResetSelection() noexcept { Selection.clear(); }
    [[nodiscard]] const std::filesystem::path     &QueryRoot() const noexcept { return Root; }
    [[nodiscard]] const std::filesystem::path     &QueryDirectory() const noexcept { return Directory; }
    [[nodiscard]] const std::vector<ContentEntry> &QueryEntries() const noexcept { return Entries; }
    [[nodiscard]] const std::vector<size_t>       &QueryVisible() const noexcept { return Visible; }
    [[nodiscard]] const ContentEntry              *QuerySelection() const noexcept;
    [[nodiscard]] const std::string               &QueryDiagnostic() const noexcept { return Diagnostic; }
    [[nodiscard]] bool                             QueryCanGoBack() const noexcept { return !Backward.empty(); }
    [[nodiscard]] bool                             QueryCanGoForward() const noexcept { return !Forward.empty(); }
    [[nodiscard]] bool                             QueryTruncated() const noexcept { return Truncated; }
    [[nodiscard]] size_t                           QueryOmittedLinks() const noexcept { return OmittedLinks; }

  private:
    bool                      ResolveEnclosed(const std::filesystem::path &Requested, std::filesystem::path &Resolved);
    bool                      Enumerate(const std::filesystem::path &Requested);
    void                      FilterEntries();
    std::filesystem::path     Root, Directory, Selection;
    std::vector<ContentEntry> Entries;
    std::vector<size_t>       Visible;
    std::vector<std::filesystem::path> Backward, Forward;
    ContentCategory                    Filter = ContentCategory::All;
    std::string                        Search, Diagnostic;
    size_t                             OmittedLinks = 0;
    bool                               Truncated    = false;
};

[[nodiscard]] const char *QueryContentCategoryLabel(ContentCategory Category) noexcept;
} // namespace Frontier
