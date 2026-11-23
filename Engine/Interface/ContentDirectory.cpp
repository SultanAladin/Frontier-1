/*==========================================================================================================================================
                                                             CONTENTDIRECTORY.CPP
==========================================================================================================================================*/
// 🧩 Locate packaged engine content independently of the working directory.

#include "ContentDirectory.h"
#include <algorithm>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <vector>
#endif

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                   DIRECTORY RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

std::filesystem::path ResolveContentDirectory(const std::filesystem::path &Requested)
{
    namespace fs = std::filesystem;
    std::error_code Failure;
    if (Requested.is_absolute())
        return fs::absolute(Requested);
    const auto Generic = Requested.generic_string();
    if (Generic != "EngineContent" && Generic != "EngineContent/FontArchives" && Generic != "EngineContent/Icons" && Generic != "EngineContent/Defaults")
        return fs::absolute(Requested);

    fs::path Executable;
#ifdef _WIN32
    wchar_t     ExecutablePath[32768];
    const DWORD Length = GetModuleFileNameW(nullptr, ExecutablePath, 32768);
    if (Length > 0 && Length < 32768)
        Executable = std::wstring(ExecutablePath, Length);
#elif defined(__linux__)
    Executable = fs::read_symlink("/proc/self/exe", Failure);
#elif defined(__APPLE__)
    uint32_t Capacity = 0;
    _NSGetExecutablePath(nullptr, &Capacity);
    std::vector<char> ExecutablePath(Capacity);
    if (_NSGetExecutablePath(ExecutablePath.data(), &Capacity) == 0)
        Executable = ExecutablePath.data();
#endif
    if (!Executable.empty())
    {
        for (auto Directory = Executable.parent_path(); !Directory.empty();)
        {
            const auto Candidate = Directory / Requested;
            if (fs::is_directory(Candidate, Failure))
                return Candidate;
            const auto Enclosure = Directory.parent_path();
            if (Enclosure == Directory)
                break;
            Directory = Enclosure;
        }
    }
    return fs::absolute(Requested);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   DIRECTORY CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
std::string FoldContentText(std::string Text)
{
    // UTF-8 paths are preserved; case-insensitive matching covers ASCII, not full Unicode case folding.
    for (auto &Character : Text)
        if (Character >= 'A' && Character <= 'Z')
            Character = static_cast<char>(Character + ('a' - 'A'));
    return Text;
}

ContentCategory ClassifyContent(const std::filesystem::path &Path)
{
    const auto Extension = FoldContentText(EncodeContentPath(Path.extension()));
    if (Extension == ".ttf" || Extension == ".otf")
        return ContentCategory::Typeface;
    if (Extension == ".svg")
        return ContentCategory::VectorImage;
    if (Extension == ".png" || Extension == ".jpg" || Extension == ".jpeg" || Extension == ".bmp")
        return ContentCategory::RasterImage;
    if (Extension == ".toml" || Extension == ".json" || Extension == ".projectspace")
        return ContentCategory::Descriptor;
    return ContentCategory::Other;
}

void AppendHistory(std::vector<std::filesystem::path> &History, const std::filesystem::path &Path)
{
    if (History.size() == 128)
        History.erase(History.begin());
    History.push_back(Path);
}
} // namespace

const char *QueryContentCategoryLabel(ContentCategory Category) noexcept
{
    switch (Category)
    {
    case ContentCategory::All:
        return "All files";
    case ContentCategory::Directory:
        return "Folders";
    case ContentCategory::Typeface:
        return "Typefaces";
    case ContentCategory::VectorImage:
        return "SVG icons";
    case ContentCategory::RasterImage:
        return "Images";
    case ContentCategory::Descriptor:
        return "Descriptors";
    default:
        return "Other files";
    }
}

bool ContentDirectory::ConfigureRoot(const std::filesystem::path &Requested)
{
    Root.clear();
    Directory.clear();
    Selection.clear();
    Entries.clear();
    Visible.clear();
    Backward.clear();
    Forward.clear();
    Diagnostic.clear();
    OmittedLinks = 0;
    Truncated    = false;
    std::error_code Failure;
    const auto      Resolved = std::filesystem::canonical(ResolveContentDirectory(Requested), Failure);
    if (Failure || !std::filesystem::is_directory(Resolved, Failure))
    {
        Diagnostic = "Content root unavailable: " + (Failure ? Failure.message() : "not a directory");
        return false;
    }
    Root = Directory = Resolved;
    return Refresh();
}

bool ContentDirectory::ResolveEnclosed(const std::filesystem::path &Requested, std::filesystem::path &Resolved)
{
    if (Root.empty())
    {
        Diagnostic = "Content root is unavailable.";
        return false;
    }
    const auto      Candidate = Requested.is_absolute() ? Requested : Directory / Requested;
    std::error_code Failure;
    Resolved = std::filesystem::canonical(Candidate, Failure);
    if (Failure)
    {
        Diagnostic = "Content path unavailable: " + Failure.message();
        return false;
    }
    auto RootPart = Root.begin();
    auto PathPart = Resolved.begin();
    for (; RootPart != Root.end() && PathPart != Resolved.end() && *RootPart == *PathPart; ++RootPart, ++PathPart)
    {
    }
    if (RootPart != Root.end())
    {
        Diagnostic = "Navigation is confined to the content root.";
        return false;
    }
    // Reject link components even when a link happens to resolve back inside the root.
    const auto Relative = Candidate.lexically_normal().lexically_relative(Root);
    auto       Prefix   = Root;
    for (const auto &Component : Relative)
    {
        Prefix /= Component;
        const auto Status = std::filesystem::symlink_status(Prefix, Failure);
        if (Failure || std::filesystem::is_symlink(Status))
        {
            Diagnostic = "Linked or unavailable content paths are not browsable.";
            return false;
        }
    }
    return true;
}

bool ContentDirectory::Enumerate(const std::filesystem::path &Requested)
{
    std::error_code                     Failure;
    std::filesystem::directory_iterator Cursor(Requested, Failure), End;
    if (Failure)
    {
        Diagnostic = "Cannot read directory: " + Failure.message();
        return false;
    }
    std::vector<ContentEntry> Found;
    size_t                    Links     = 0;
    size_t                    Inspected = 0;
    bool                      Limited   = false;
    for (; Cursor != End; Cursor.increment(Failure))
    {
        if (Failure)
            break;
        if (Inspected++ == EntryLimit)
        {
            Limited = true;
            break;
        }
        const auto Status = Cursor->symlink_status(Failure);
        if (Failure)
            break;
        if (std::filesystem::is_symlink(Status))
        {
            ++Links;
            continue;
        }
        if (!std::filesystem::is_directory(Status) && !std::filesystem::is_regular_file(Status))
            continue;
        ContentEntry Entry;
        Entry.Path = Cursor->path();
        Entry.Name = EncodeContentPath(Entry.Path.filename());
        Entry.Category =
            std::filesystem::is_directory(Status) ? ContentCategory::Directory : ClassifyContent(Entry.Path);
        if (Entry.Category != ContentCategory::Directory)
        {
            std::error_code SizeFailure;
            Entry.ByteCount      = Cursor->file_size(SizeFailure);
            Entry.ByteCountKnown = !SizeFailure;
        }
        Found.push_back(std::move(Entry));
    }
    if (Failure)
    {
        Diagnostic = "Directory changed or could not be read: " + Failure.message();
        return false;
    }
    std::sort(Found.begin(),
              Found.end(),
              [](const ContentEntry &Left, const ContentEntry &Right)
              {
                  const bool LeftDirectory  = Left.Category == ContentCategory::Directory;
                  const bool RightDirectory = Right.Category == ContentCategory::Directory;
                  if (LeftDirectory != RightDirectory)
                      return LeftDirectory;
                  const auto LeftName  = FoldContentText(Left.Name);
                  const auto RightName = FoldContentText(Right.Name);
                  return LeftName == RightName ? Left.Name < Right.Name : LeftName < RightName;
              });
    Entries      = std::move(Found);
    Directory    = Requested;
    OmittedLinks = Links;
    Truncated    = Limited;
    Diagnostic.clear();
    FilterEntries();
    return true;
}

bool ContentDirectory::Navigate(const std::filesystem::path &Requested)
{
    std::filesystem::path Resolved;
    if (!ResolveEnclosed(Requested, Resolved))
        return false;
    if (Resolved == Directory)
        return Refresh();
    const auto Previous = Directory;
    if (!Enumerate(Resolved))
        return false;
    AppendHistory(Backward, Previous);
    Forward.clear();
    Selection.clear();
    return true;
}

bool ContentDirectory::NavigateUp()
{
    if (Directory.empty() || Directory == Root)
        return false;
    return Navigate(Directory.parent_path());
}

bool ContentDirectory::NavigateBack()
{
    if (Backward.empty())
        return false;
    std::filesystem::path Resolved;
    if (!ResolveEnclosed(Backward.back(), Resolved))
        return false;
    const auto Previous = Directory;
    if (!Enumerate(Resolved))
        return false;
    Backward.pop_back();
    AppendHistory(Forward, Previous);
    Selection.clear();
    return true;
}

bool ContentDirectory::NavigateForward()
{
    if (Forward.empty())
        return false;
    std::filesystem::path Resolved;
    if (!ResolveEnclosed(Forward.back(), Resolved))
        return false;
    const auto Previous = Directory;
    if (!Enumerate(Resolved))
        return false;
    Forward.pop_back();
    AppendHistory(Backward, Previous);
    Selection.clear();
    return true;
}

bool ContentDirectory::Refresh()
{
    std::filesystem::path Resolved;
    if (!ResolveEnclosed(Directory, Resolved) || !Enumerate(Resolved))
    {
        Entries.clear();
        Visible.clear();
        Selection.clear();
        OmittedLinks = 0;
        Truncated    = false;
        return false;
    }
    return true;
}

void ContentDirectory::ConfigureFilter(ContentCategory Category, std::string Text)
{
    Filter = Category;
    Search = FoldContentText(std::move(Text));
    FilterEntries();
}

void ContentDirectory::FilterEntries()
{
    Visible.clear();
    bool SelectionVisible = false;
    for (size_t Ordinal = 0; Ordinal < Entries.size(); ++Ordinal)
    {
        const auto &Entry = Entries[Ordinal];
        // Directories remain navigable under type filters, but still respect the filename query.
        if (Filter != ContentCategory::All && Entry.Category != Filter && Entry.Category != ContentCategory::Directory)
            continue;
        // 📝 The seek run spans the naming, the extension and the category spelling, so a run reaching
        // across any of them retains the entry, exactly as the reference lattice filters it.
        if (!Search.empty() &&
            (FoldContentText(Entry.Name) + " " + FoldContentText(EncodeContentPath(Entry.Path.extension())) + " " +
             FoldContentText(QueryContentCategoryLabel(Entry.Category)))
                    .find(Search) == std::string::npos)
            continue;
        Visible.push_back(Ordinal);
        SelectionVisible = SelectionVisible || Entry.Path == Selection;
    }
    if (!SelectionVisible)
        Selection.clear();
}

bool ContentDirectory::Select(const std::filesystem::path &Requested)
{
    Selection.clear();
    std::filesystem::path Resolved;
    if (!ResolveEnclosed(Requested, Resolved))
        return false;
    for (const auto Ordinal : Visible)
        if (Entries[Ordinal].Path == Resolved)
        {
            Selection = Resolved;
            Diagnostic.clear();
            return true;
        }
    Diagnostic = "Selection is not in the current filtered directory.";
    return false;
}

const ContentEntry *ContentDirectory::QuerySelection() const noexcept
{
    if (!Selection.empty())
        for (const auto &Entry : Entries)
            if (Entry.Path == Selection)
                return &Entry;
    return nullptr;
}
} // namespace Frontier
