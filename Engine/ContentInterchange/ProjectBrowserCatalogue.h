/*==========================================================================================================================================
                                                         PROJECTBROWSERCATALOGUE.H
==========================================================================================================================================*/
// 🧩 Sample file catalogue, filters, stars/notes preferences and Markdown subset transcribed from the HTML browser (Browser.js).

#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     SAMPLE CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

enum class BrowserFileCategory
{
    Project,
    Cad,
    Geometry,
    Document
};

struct BrowserSampleNote
{
    std::string Author;
    std::string Initial;
    std::string Text;
    std::string Time;
};

struct BrowserCatalogueEntry
{
    std::string Token;
    std::string Title;
    std::string Extension;
    BrowserFileCategory Category = BrowserFileCategory::Project;
    std::string Description;
    std::string Art;
    std::string Context;
    std::string Opened;
    std::string Path;
    std::vector<std::string> Views;
    std::vector<BrowserSampleNote> Notes;
    std::vector<std::string> Revisions;
    bool Imported = false;
    uint64_t ByteCount = 0;
    std::vector<uint8_t> ImageBytes;
    std::string Markdown;
};

// 📝 The six HTML fixtures verbatim (titles, descriptions, views, notes, revisions, sample paths).
inline std::vector<BrowserCatalogueEntry> ConstructBrowserCatalogue()
{
    return {
        {"valley", "Actuator studio", ".project", BrowserFileCategory::Project,
            "A small studio scene around the reference actuator assembly.", "Valley", "3 levels · 1 document",
            "Today, 10:42", "D:/Frontier/Projects/Actuator studio/Studio.project",
            {"Studio", "Courtyard", "Workshop"},
            {{"Sam", "S", "Review the clearance around the central specimen.", "2 hours ago"},
                {"Alex", "A", "The alternate studio camera is ready for review.", "Yesterday"}},
            {"Copper winding and studio props", "Shaft and fasteners", "Housing blockout"}},
        {"mount", "Precision mount", ".assembly", BrowserFileCategory::Cad,
            "A compact mounting assembly, built around a machined central sleeve.", "Assembly", "2 assemblies · 2 sketches",
            "Today, 09:18", "D:/Frontier/CAD/Precision mount/Mount.assembly",
            {"Main assembly", "Sleeve assembly", "Sketch 01", "Sketch 02"},
            {{"Sam", "S", "Check the clearance around the sleeve before the next revision.", "45 minutes ago"}},
            {"Copper winding rings", "Shaft and fasteners", "Initial housing"}},
        {"stone", "Actuator housing", ".geometry", BrowserFileCategory::Geometry,
            "Actuator housing geometry extracted from the reference specimen.", "Geometry", "Geometry · UV · Texture",
            "Yesterday", "D:/Frontier/Geometry/Actuator housing/Housing.geometry",
            {"Geometry", "UV layout", "Texture", "Metadata"},
            {{"Alex", "A", "Keep the UV seams on the underside.", "Yesterday"}},
            {"Full housing proportions", "Reduced housing study", "Initial compact housing"}},
        {"island", "Studio exploration", ".project", BrowserFileCategory::Project,
            "Three camera views of the reference specimen in a compact studio.", "Valley", "3 levels · 1 document",
            "Yesterday", "E:/Studies/Island/Island.project", {"Studio", "Courtyard", "Workshop"}, {},
            {"Lookout composition", "Crossing layout", "Initial project"}},
        {"bracket", "Actuator sketches", ".sketch", BrowserFileCategory::Cad,
            "Orthographic contour views of the reference actuator specimen.", "Assembly", "3 sketches · 1 document",
            "Oct 14", "E:/Design/Bracket/Bracket.sketch", {"Sketch 01", "Sketch 02", "Sketch 03"}, {},
            {"Mounting hole positions", "Outer contour", "Initial sketch"}},
        {"faceted", "Housing study", ".geometry", BrowserFileCategory::Geometry,
            "A second housing inspection fixture using the reference surface renderer.", "Geometry", "Geometry · UV · Texture",
            "Oct 12", "D:/Studies/Faceted/Surface.geometry", {"Geometry", "UV layout", "Texture", "Metadata"}, {},
            {"Surface colour study", "UV unwrap", "Initial geometry"}},
    };
}

inline std::string FoldBrowserText(std::string Text)
{
    for (auto &Character : Text) Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
    return Text;
}

inline const BrowserCatalogueEntry *FindBrowserEntry(const std::vector<BrowserCatalogueEntry> &Catalogue, const std::string &Token)
{
    for (const auto &Entry : Catalogue)
        if (Entry.Token == Token) return &Entry;
    return nullptr;
}

// 📝 `recent` intentionally passes every entry, matching the HTML filter predicate.
inline std::vector<const BrowserCatalogueEntry *> FilterBrowserCatalogue(const std::vector<BrowserCatalogueEntry> &Catalogue,
    const std::string &Filter, const std::string &Search, const std::set<std::string> &Stars, bool SortByName)
{
    std::vector<const BrowserCatalogueEntry *> Entries;
    const std::string Query = FoldBrowserText(Search);
    const size_t First = Query.find_first_not_of(" \t\r\n\f\v"), Last = Query.find_last_not_of(" \t\r\n\f\v");
    const std::string Trimmed = First == std::string::npos ? "" : Query.substr(First, Last - First + 1);
    for (const auto &Entry : Catalogue)
    {
        const bool MatchesSearch = Trimmed.empty() ||
            FoldBrowserText(Entry.Title + " " + Entry.Extension + " " +
                (Entry.Category == BrowserFileCategory::Project ? "project" : Entry.Category == BrowserFileCategory::Cad ? "cad" : Entry.Category == BrowserFileCategory::Geometry ? "geometry" : "document"))
                .find(Trimmed) != std::string::npos;
        const bool MatchesFilter = Filter == "all" || Filter == "recent" || (Filter == "starred" && Stars.count(Entry.Token)) ||
            (Filter == "project" && Entry.Category == BrowserFileCategory::Project) ||
            (Filter == "cad" && Entry.Category == BrowserFileCategory::Cad) ||
            (Filter == "geometry" && Entry.Category == BrowserFileCategory::Geometry);
        if (MatchesSearch && MatchesFilter) Entries.push_back(&Entry);
    }
    if (SortByName)
        std::sort(Entries.begin(), Entries.end(), [](const auto *First, const auto *Second) { return First->Title < Second->Title; });
    return Entries;
}

inline std::string BrowserCollectionTitle(const std::string &Filter)
{
    if (Filter == "recent") return "Recently opened";
    if (Filter == "starred") return "Starred files";
    if (Filter == "project") return "Projects";
    if (Filter == "cad") return "CAD";
    if (Filter == "geometry") return "Geometry";
    return "Your workspace";
}

inline std::string BrowserCollectionCount(const std::vector<const BrowserCatalogueEntry *> &Entries)
{
    const bool Samples = std::all_of(Entries.begin(), Entries.end(), [](const auto *Entry) { return !Entry->Imported; });
    return std::to_string(Entries.size()) + (Samples ? " sample file" : " file") + (Entries.size() == 1 ? "" : "s");
}

inline std::string BrowserRailTitle(const BrowserCatalogueEntry &Entry)
{
    if (Entry.Category == BrowserFileCategory::Project) return "Levels";
    if (Entry.Category == BrowserFileCategory::Cad) return Entry.Extension == ".sketch" ? "Sketches" : "Assembly";
    if (Entry.Category == BrowserFileCategory::Geometry) return "Views";
    return "Document";
}

inline std::string BrowserWorkspaceLabel(BrowserFileCategory Category)
{
    if (Category == BrowserFileCategory::Project) return "Game project";
    if (Category == BrowserFileCategory::Cad) return "Drafting / CAD";
    if (Category == BrowserFileCategory::Geometry) return "Geometry";
    return "Document";
}

inline std::string BrowserCountsLabel(BrowserFileCategory Category)
{
    if (Category == BrowserFileCategory::Project) return "Levels";
    if (Category == BrowserFileCategory::Cad) return "Assemblies / sketches";
    return "Preview views";
}

inline std::string BrowserCategoryLabel(BrowserFileCategory Category)
{
    if (Category == BrowserFileCategory::Project) return "Project";
    if (Category == BrowserFileCategory::Cad) return "CAD";
    if (Category == BrowserFileCategory::Geometry) return "Geometry";
    return "Document";
}

inline std::string BrowserLaunchTarget(BrowserFileCategory Category)
{
    if (Category == BrowserFileCategory::Project) return "Frontier · Project workspace";
    if (Category == BrowserFileCategory::Cad) return "Frontier · Drafting workspace";
    if (Category == BrowserFileCategory::Geometry) return "Frontier · Geometry workspace";
    return "Frontier · Document preview";
}

inline std::string BrowserRevisionDate(size_t Index)
{
    if (Index == 0) return "Today";
    if (Index == 1) return "October 15, 2026";
    return "October 12, 2026";
}

inline std::string BrowserVersionLabel(size_t Revisions, size_t Index)
{
    return "Version " + std::to_string(Revisions - Index) + (Index == 0 ? " · Current" : "");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     MARKDOWN SUBSET
//------------------------------------------------------------------------------------------------------------------------

struct BrowserMarkdownBlock
{
    int Level = 0; // 0 paragraph, 1 h1, 2 h2
    std::string Text;
};

inline std::vector<BrowserMarkdownBlock> ParseBrowserMarkdown(const std::string &Text)
{
    const std::string Clipped = Text.substr(0, 262144);
    std::vector<BrowserMarkdownBlock> Blocks;
    size_t Begin = 0;
    const auto Push = [&](size_t End)
    {
        std::string Paragraph = Clipped.substr(Begin, End - Begin);
        while (!Paragraph.empty() && (Paragraph.back() == '\n' || Paragraph.back() == '\r' || Paragraph.back() == ' ' || Paragraph.back() == '\t'))
            Paragraph.pop_back();
        if (Paragraph.empty()) return;
        if (Paragraph.rfind("# ", 0) == 0) Blocks.push_back({1, Paragraph.substr(2)});
        else if (Paragraph.rfind("## ", 0) == 0) Blocks.push_back({2, Paragraph.substr(3)});
        else Blocks.push_back({0, Paragraph});
    };
    for (size_t Scan = 0; Scan < Clipped.size();)
    {
        // 📝 Split on /\n\s*\n/: a newline, optional blank-line whitespace, then a newline.
        size_t Break = std::string::npos, Resume = std::string::npos;
        for (size_t At = Scan; At < Clipped.size(); ++At)
        {
            if (Clipped[At] != '\n') continue;
            size_t Skip = At + 1;
            while (Skip < Clipped.size() &&
                (Clipped[Skip] == ' ' || Clipped[Skip] == '\t' || Clipped[Skip] == '\r' || Clipped[Skip] == '\f' ||
                    Clipped[Skip] == '\v'))
                ++Skip;
            if (Skip < Clipped.size() && Clipped[Skip] == '\n')
            {
                Break = At;
                Resume = Skip + 1;
                while (Resume < Clipped.size() &&
                    (Clipped[Resume] == '\n' || Clipped[Resume] == '\r' || Clipped[Resume] == ' ' ||
                        Clipped[Resume] == '\t'))
                    ++Resume;
                break;
            }
        }
        if (Break == std::string::npos) { Push(Clipped.size()); break; }
        Push(Break);
        Begin = Resume;
        Scan = Resume;
    }
    return Blocks;
}

inline std::string ConstructBrowserOverview(const BrowserCatalogueEntry &Entry, size_t Revision)
{
    std::string Sequence;
    for (size_t Index = 0; Index < Entry.Views.size(); ++Index)
        Sequence += (Index ? "\n" : "") + std::to_string(Index + 1) + ". " + Entry.Views[Index];
    const std::string RevisionText = Revision < Entry.Revisions.size() ? Entry.Revisions[Revision] : "Local document";
    return "# " + Entry.Title + "\n\n" + Entry.Description + "\n\n## Preview sequence\n\n" + Sequence + "\n\n## Revision " +
        std::to_string(Entry.Revisions.size() > Revision ? Entry.Revisions.size() - Revision : 0) + "\n\n" + RevisionText +
        "\n\n## Preview\n\nThe image below is captured from the same reference renderer as the live review. Sample revisions are browser fixtures, not an on-disk history archive.";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PREVIEW CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct BrowserPreviewConfiguration
{
    BrowserFileCategory Category = BrowserFileCategory::Project;
    size_t View = 0;
    size_t Revision = 0;
    bool Sketch = false;
    int Channel = 0;
    bool HiddenEdges = false;
};

inline BrowserPreviewConfiguration ConfigureBrowserPreview(const BrowserCatalogueEntry &Entry, size_t View, size_t Revision)
{
    const std::string Title = View < Entry.Views.size() ? Entry.Views[View] : "";
    BrowserPreviewConfiguration Configuration;
    Configuration.Category = Entry.Category;
    Configuration.View = View;
    Configuration.Revision = Revision;
    Configuration.Sketch = Entry.Extension == ".sketch" || Title.find("Sketch") != std::string::npos;
    Configuration.Channel = Title == "Texture" ? 1 : Entry.Category == BrowserFileCategory::Cad ? 5 : 0;
    Configuration.HiddenEdges = Entry.Category == BrowserFileCategory::Cad;
    return Configuration;
}

inline std::string BrowserSnapshotKey(const std::string &Token, size_t View, size_t Revision)
{
    return Token + ":" + std::to_string(Revision) + ":" + std::to_string(View);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LOCAL FILE IMPORT
//------------------------------------------------------------------------------------------------------------------------

inline BrowserFileCategory ClassifyBrowserImport(const std::string &Extension, bool &Supported)
{
    const std::string Folded = FoldBrowserText(Extension);
    Supported = true;
    if (Folded == ".project" || Folded == ".projectspace" || Folded == ".scene") return BrowserFileCategory::Project;
    if (Folded == ".assembly" || Folded == ".sketch") return BrowserFileCategory::Cad;
    if (Folded == ".geometry" || Folded == ".geometryspace" || Folded == ".png" || Folded == ".jpg" ||
        Folded == ".jpeg" || Folded == ".webp")
        return BrowserFileCategory::Geometry;
    if (Folded == ".md") return BrowserFileCategory::Document;
    Supported = false;
    return BrowserFileCategory::Document;
}

inline bool QueryBrowserImageExtension(const std::string &Extension)
{
    const std::string Folded = FoldBrowserText(Extension);
    return Folded == ".png" || Folded == ".jpg" || Folded == ".jpeg" || Folded == ".webp";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PREFERENCES
//------------------------------------------------------------------------------------------------------------------------

struct BrowserLocalNote
{
    std::string Author;
    std::string Initial;
    std::string Text;
    std::string Time;
};

struct BrowserPreferences
{
    std::string Theme = "oled";
    std::set<std::string> Stars{"valley", "mount"};
    std::map<std::string, std::vector<BrowserLocalNote>> Notes;
};

inline std::string EncodeBrowserTomlString(const std::string &Text)
{
    std::string Encoded = "\"";
    for (const char Character : Text)
    {
        if (Character == '"') Encoded += "\\\"";
        else if (Character == '\\') Encoded += "\\\\";
        else if (Character == '\n') Encoded += "\\n";
        else if (Character == '\r') Encoded += "\\r";
        else if (Character == '\t') Encoded += "\\t";
        else Encoded += Character;
    }
    return Encoded + "\"";
}

inline bool SaveBrowserPreferences(const std::filesystem::path &Path, const BrowserPreferences &Preferences, std::string &Diagnostic)
{
    if (Path.empty()) return true;
    std::error_code Failure;
    if (Path.has_parent_path()) std::filesystem::create_directories(Path.parent_path(), Failure);
    std::ostringstream Content;
    Content << "theme = " << EncodeBrowserTomlString(Preferences.Theme) << "\nstars = [";
    bool First = true;
    for (const auto &Star : Preferences.Stars) { Content << (First ? "" : ", ") << EncodeBrowserTomlString(Star); First = false; }
    Content << "]\n";
    for (const auto &[Token, Notes] : Preferences.Notes)
    {
        Content << "[notes." << Token << "]\nentries = [";
        bool Leading = true;
        for (const auto &Note : Notes)
        {
            Content << (Leading ? "" : ", ") << "{ author = " << EncodeBrowserTomlString(Note.Author) << ", initial = " <<
                EncodeBrowserTomlString(Note.Initial) << ", text = " << EncodeBrowserTomlString(Note.Text) << ", time = " <<
                EncodeBrowserTomlString(Note.Time) << " }";
            Leading = false;
        }
        Content << "]\n";
    }
    std::ofstream Stream(Path, std::ios::binary | std::ios::trunc);
    if (!Stream) { Diagnostic = "Preferences file is not writable."; return false; }
    Stream << Content.str();
    return !!Stream;
}

inline bool LoadBrowserPreferences(const std::filesystem::path &Path, BrowserPreferences &Preferences, std::string &Diagnostic)
{
    Preferences = BrowserPreferences{};
    if (Path.empty()) return true;
    std::ifstream Stream(Path, std::ios::binary);
    if (!Stream) return true;
    const std::string Content{std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>()};
    const auto Value = [&](const std::string &Key) -> std::string
    {
        const size_t Found = Content.find(Key + " = \"");
        if (Found == std::string::npos) return {};
        std::string Decoded;
        for (size_t Index = Found + Key.size() + 4; Index < Content.size(); ++Index)
        {
            const char Character = Content[Index];
            if (Character == '"') break;
            if (Character == '\\' && Index + 1 < Content.size())
            {
                const char Escaped = Content[++Index];
                Decoded += Escaped == 'n' ? '\n' : Escaped == 'r' ? '\r' : Escaped == 't' ? '\t' : Escaped;
            }
            else Decoded += Character;
        }
        return Decoded;
    };
    const std::string Theme = Value("theme");
    if (Theme == "oled" || Theme == "light" || Theme == "dark") Preferences.Theme = Theme;
    const size_t Stars = Content.find("stars = [");
    if (Stars != std::string::npos)
    {
        Preferences.Stars.clear();
        const size_t End = Content.find(']', Stars);
        std::string List = Content.substr(Stars, End == std::string::npos ? std::string::npos : End - Stars);
        for (size_t Index = 0; (Index = List.find('"', Index)) != std::string::npos;)
        {
            const size_t Close = List.find('"', Index + 1);
            if (Close == std::string::npos) break;
            Preferences.Stars.insert(List.substr(Index + 1, Close - Index - 1));
            Index = Close + 1;
        }
    }
    for (size_t Section = 0; (Section = Content.find("[notes.", Section)) != std::string::npos;)
    {
        const size_t Close = Content.find(']', Section);
        if (Close == std::string::npos) break;
        const std::string Token = Content.substr(Section + 7, Close - Section - 7);
        const size_t Next = Content.find("[notes.", Close);
        const std::string Body = Content.substr(Close, Next == std::string::npos ? std::string::npos : Next - Close);
        std::vector<BrowserLocalNote> Notes;
        for (size_t Entry = 0; (Entry = Body.find("{ author = \"", Entry)) != std::string::npos;)
        {
            BrowserLocalNote Note;
            size_t Cursor = Entry + 11;
            for (auto *Field : {&Note.Author, &Note.Initial, &Note.Text, &Note.Time})
            {
                const size_t Open = Body.find('"', Cursor);
                if (Open == std::string::npos) break;
                std::string Decoded;
                size_t Index = Open + 1;
                for (; Index < Body.size(); ++Index)
                {
                    if (Body[Index] == '"') break;
                    if (Body[Index] == '\\' && Index + 1 < Body.size())
                    {
                        const char Escaped = Body[++Index];
                        Decoded += Escaped == 'n' ? '\n' : Escaped == 'r' ? '\r' : Escaped == 't' ? '\t' : Escaped;
                    }
                    else Decoded += Body[Index];
                }
                *Field = Decoded;
                Cursor = Index + 1;
            }
            Notes.push_back(Note);
            Entry = Cursor;
        }
        if (!Notes.empty()) Preferences.Notes[Token] = Notes;
        Section = Close + 1;
    }
    Diagnostic.clear();
    return true;
}
} // namespace Frontier
