/*==========================================================================================================================================
                                                           MATERIALLIBRARY.CPP
==========================================================================================================================================*/
// 🧩 Discover authored TOML materials and retain last-valid compilation across edits, failures and explicit disk saves.

#include "MaterialLibrary.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Render/MaterialProceduralBaking.h"
#include <charconv>
#include <toml++/toml.hpp>
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/TextFileReplacement.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <functional>
#include <optional>
#include <sstream>
#include <string_view>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Frontier
{
namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     MATERIAL FILE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

std::string FoldMaterialText(std::string Text)
{
    for (auto &Character : Text)
        if (Character >= 'A' && Character <= 'Z') Character += 'a'-'A';
    return Text;
}

std::filesystem::path NormalizeMaterialPath(const std::filesystem::path &Path)
{
    // 📝 lexical normalization uses native separators on Windows; retain portable UTF-8 separators in library selections.
    return std::filesystem::path(Path.lexically_normal().generic_u8string());
}

// 📝 Line surgery for stratum/channel blocks: structured edits splice source text so comments and formatting
// survive, and every splice re-decodes before commit, keeping hand edits and panel edits interchangeable.
struct SourceSpan
{
    size_t Begin = 0, End = 0;
};

std::string_view TrimSourceLine(std::string_view Line)
{
    while (!Line.empty() && (Line.front() == ' ' || Line.front() == '\t' || Line.front() == '\r')) Line.remove_prefix(1);
    while (!Line.empty() && (Line.back() == ' ' || Line.back() == '\t' || Line.back() == '\r')) Line.remove_suffix(1);
    return Line;
}

bool IsStrataHeader(std::string_view Trimmed) { return Trimmed.starts_with("[[Strata]]"); }

bool IsTopLevelHeader(std::string_view Trimmed)
{
    if (!Trimmed.starts_with("[") || Trimmed.starts_with("[[")) return false;
    const auto Close = Trimmed.find(']');
    return Close != std::string_view::npos && Trimmed.substr(0, Close).find('.') == std::string_view::npos;
}

std::vector<SourceSpan> SplitStrataBlocks(const std::string &Source)
{
    std::vector<SourceSpan> Blocks;
    size_t Cursor = 0;
    int Open = -1;
    while (Cursor <= Source.size())
    {
        const size_t Stop = Source.find('\n', Cursor);
        const size_t End = Stop == std::string::npos ? Source.size() : Stop;
        const auto Trimmed = TrimSourceLine(std::string_view(Source.data() + Cursor, End - Cursor));
        if (IsStrataHeader(Trimmed))
        {
            if (Open >= 0) Blocks[size_t(Open)].End = Cursor;
            Open = int(Blocks.size());
            Blocks.push_back({Cursor, Source.size()});
        }
        else if (IsTopLevelHeader(Trimmed) && Open >= 0)
        {
            Blocks[size_t(Open)].End = Cursor;
            Open = -1;
        }
        if (Stop == std::string::npos) break;
        Cursor = Stop + 1;
    }
    return Blocks;
}

struct ChannelSpan
{
    SourceSpan Span;
    std::string Name;
};

bool IsChannelParamsHeader(std::string_view Trimmed) { return Trimmed.starts_with("[Strata.Channels.Params]"); }

std::vector<ChannelSpan> SplitChannelBlocks(const std::string &Source, SourceSpan Block)
{
    std::vector<ChannelSpan> Channels;
    size_t Cursor = Block.Begin;
    int Open = -1;
    while (Cursor < Block.End)
    {
        const size_t Stop = Source.find('\n', Cursor);
        const size_t End = Stop == std::string::npos || Stop > Block.End ? Block.End : Stop;
        const auto Trimmed = TrimSourceLine(std::string_view(Source.data() + Cursor, End - Cursor));
        const bool Header = Trimmed.starts_with("[");
        if (Trimmed.starts_with("[[Strata.Channels]]"))
        {
            if (Open >= 0) Channels[size_t(Open)].Span.End = Cursor;
            Open = int(Channels.size());
            Channels.push_back({{Cursor, Block.End}, {}});
        }
        else if (Header && !IsChannelParamsHeader(Trimmed) && Open >= 0)
        {
            Channels[size_t(Open)].Span.End = Cursor;
            Open = -1;
        }
        else if (Open >= 0 && Channels[size_t(Open)].Name.empty() && Trimmed.starts_with("Name"))
        {
            const auto Equals = Trimmed.find('=');
            const auto First = Equals == std::string_view::npos ? std::string_view::npos : Trimmed.find('"', Equals);
            const auto Last = First == std::string_view::npos ? std::string_view::npos : Trimmed.find('"', First + 1);
            if (First != std::string_view::npos && Last != std::string_view::npos)
                Channels[size_t(Open)].Name = std::string(Trimmed.substr(First + 1, Last - First - 1));
        }
        if (Stop == std::string::npos || Stop >= Block.End) break;
        Cursor = Stop + 1;
    }
    return Channels;
}

SourceSpan FindTexturesSection(const std::string &Source, SourceSpan Block)
{
    size_t Cursor = Block.Begin;
    bool Open = false;
    SourceSpan Section{0, 0};
    while (Cursor < Block.End)
    {
        const size_t Stop = Source.find('\n', Cursor);
        const size_t End = Stop == std::string::npos || Stop > Block.End ? Block.End : Stop;
        const auto Trimmed = TrimSourceLine(std::string_view(Source.data() + Cursor, End - Cursor));
        if (Trimmed.starts_with("[Strata.Textures]"))
        {
            Open = true;
            Section.Begin = Cursor;
            Section.End = Block.End;
        }
        else if (Open && Trimmed.starts_with("["))
        {
            Section.End = Cursor;
            break;
        }
        if (Stop == std::string::npos || Stop >= Block.End) break;
        Cursor = Stop + 1;
    }
    return Open ? Section : SourceSpan{0, 0};
}

SourceSpan FindKeyLine(const std::string &Source, SourceSpan Span, std::string_view Key)
{
    size_t Cursor = Span.Begin;
    while (Cursor < Span.End)
    {
        const size_t Stop = Source.find('\n', Cursor);
        const size_t End = Stop == std::string::npos || Stop > Span.End ? Span.End : Stop;
        const auto Trimmed = TrimSourceLine(std::string_view(Source.data() + Cursor, End - Cursor));
        if (Trimmed.starts_with(Key))
        {
            const auto Rest = TrimSourceLine(Trimmed.substr(Key.size()));
            if (Rest.starts_with("=")) return {Cursor, Stop == std::string::npos ? Span.End : Stop + 1};
        }
        if (Stop == std::string::npos || Stop >= Span.End) break;
        Cursor = Stop + 1;
    }
    return {0, 0};
}

std::string EncodeSourceDouble(double Value)
{
    char Number[64]{};
    const auto Encoded = std::to_chars(Number, Number + sizeof(Number), Value);
    if (Encoded.ec != std::errc{}) throw std::runtime_error("Material coefficient encoding failed.");
    std::string Numeric(Number, Encoded.ptr);
    if (Numeric.find_first_of(".eE") == std::string::npos) Numeric += ".0";
    return Numeric;
}

std::string DecodeMaterialSource(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary | std::ios::ate);
    const auto Bytes = Input.tellg();
    if (!Input || Bytes < 0 || Bytes > 1048576) throw std::runtime_error("Material source is missing or exceeds 1 MiB.");
    std::string Source(static_cast<size_t>(Bytes), '\0');
    Input.seekg(0);
    Input.read(Source.data(), Bytes);
    if (!Input) throw std::runtime_error("Material source could not be read completely.");
    return Source;
}

void CreateMaterialSource(const std::filesystem::path &Path, const std::string &Source)
{
    // 📝 Exclusive creation refuses an existing file even if another writer creates it after path validation.
#ifdef _WIN32
    HANDLE File = CreateFileW(Path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (File == INVALID_HANDLE_VALUE) throw std::runtime_error("Material destination exists or cannot be created.");
    DWORD Written = 0;
    const bool Complete = WriteFile(File, Source.data(), static_cast<DWORD>(Source.size()), &Written, nullptr) &&
                          Written == Source.size() && FlushFileBuffers(File);
    const bool Closed = CloseHandle(File) != FALSE;
#else
    const int File = open(Path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (File < 0) throw std::runtime_error("Material destination exists or cannot be created.");
    size_t Written = 0;
    while (Written < Source.size())
    {
        const auto Count = write(File, Source.data()+Written, Source.size()-Written);
        if (Count < 0 && errno == EINTR) continue;
        if (Count <= 0) break;
        Written += static_cast<size_t>(Count);
    }
    const bool Complete = Written == Source.size() && fsync(File) == 0;
    const bool Closed = close(File) == 0;
#endif
    if (!Complete || !Closed)
    {
        std::error_code Ignored;
        std::filesystem::remove(Path, Ignored);
        throw std::runtime_error("New material could not be written completely.");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SAND BED NORMALIZATION
//------------------------------------------------------------------------------------------------------------------------
// 📝 normalizeSandLayers parity: up to four beds, Alloy defaults for missing fields, finite clamps per key.

double SandFinite(double Value, bool Present, double Fallback, double Lo, double Hi)
{
    if (!Present || !std::isfinite(Value)) return Fallback;
    return std::clamp(Value, Lo, Hi);
}

std::string CleanSandHex(std::string_view Value, bool Present, const std::string &Fallback)
{
    if (!Present || Value.size() != 7 || Value[0] != '#') return Fallback;
    for (const char Digit : Value.substr(1))
        if (!((Digit >= '0' && Digit <= '9') || (Digit >= 'a' && Digit <= 'f') || (Digit >= 'A' && Digit <= 'F')))
            return Fallback;
    std::string Lower(Value);
    for (auto &Character : Lower)
        if (Character >= 'A' && Character <= 'F') Character += 'a' - 'A';
    return Lower;
}

bool ValidSandBedId(std::string_view Id)
{
    if (Id.empty() || Id.size() > 60) return false;
    for (const char Character : Id)
        if (!((Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') ||
                (Character >= '0' && Character <= '9') || Character == '_' || Character == '-'))
            return false;
    return true;
}

MaterialSandBed DefaultSandBed(size_t Index)
{
    MaterialSandBed Bed;
    Bed.Id = "sand-bed-" + std::to_string(Index + 1);
    return Bed;
}

void ClampSandBedNumerics(MaterialSandBed &Bed,
    const std::function<std::optional<double>(const char *)> &Number, const MaterialSandBed &Defaults)
{
    const auto Finite = [&](const char *Key, double Fallback, double Lo, double Hi)
    {
        const auto Value = Number(Key);
        return SandFinite(Value.value_or(Fallback), Value.has_value(), Fallback, Lo, Hi);
    };
    Bed.Size = Finite("size", Defaults.Size, .0625, 2);
    Bed.SizeVariation = Finite("sizeVariation", Defaults.SizeVariation, 0, 1);
    Bed.Roughness = Finite("roughness", Defaults.Roughness, .04, 1);
    Bed.RoughnessVariation = Finite("roughnessVariation", Defaults.RoughnessVariation, 0, 1);
    Bed.Specular = Finite("specular", Defaults.Specular, 0, 1);
    Bed.SpecularVariation = Finite("specularVariation", Defaults.SpecularVariation, 0, 1);
    Bed.Ior = Finite("ior", Defaults.Ior, 1.3, 2.2);
    Bed.Roundness = Finite("roundness", Defaults.Roundness, 0, 1);
    Bed.Tilt = Finite("tilt", Defaults.Tilt, 0, 1);
    Bed.Relief = Finite("relief", Defaults.Relief, 0, 1);
    Bed.Coverage = Finite("coverage", Defaults.Coverage, 0, 1);
    Bed.Thickness = Finite("thickness", Defaults.Thickness, .1, 4);
}

void ParseSandBeds(const toml::node *Layers, std::vector<MaterialSandBed> &Beds)
{
    Beds.clear();
    const toml::array *Items = Layers ? Layers->as_array() : nullptr;
    if (!Items || Items->empty())
    {
        Beds.push_back(DefaultSandBed(0));
        return;
    }
    std::vector<std::string> Used;
    size_t Index = 0;
    for (const auto &Item : *Items)
    {
        if (Index >= 4) break;
        const MaterialSandBed Defaults = DefaultSandBed(Index);
        MaterialSandBed Bed = Defaults;
        std::string Candidate = Defaults.Id;
        if (const toml::table *Fields = Item.as_table())
        {
            const auto Id = (*Fields)["id"].value<std::string>();
            Candidate = (Id && ValidSandBedId(*Id) &&
                    std::find(Used.begin(), Used.end(), *Id) == Used.end())
                ? *Id
                : Defaults.Id;
            Bed.Enabled = (*Fields)["enabled"].value_or(true);
            const auto Color = (*Fields)["color"].value<std::string>();
            Bed.Color = CleanSandHex(Color.value_or(""), Color.has_value(), Defaults.Color);
            const auto Secondary = (*Fields)["secondaryColor"].value<std::string>();
            Bed.SecondaryColor = CleanSandHex(Secondary.value_or(""), Secondary.has_value(), Defaults.SecondaryColor);
            ClampSandBedNumerics(Bed, [&](const char *Key) { return (*Fields)[Key].value<double>(); }, Defaults);
        }
        while (std::find(Used.begin(), Used.end(), Candidate) != Used.end()) Candidate += "-copy";
        Bed.Id = Candidate;
        Used.push_back(Bed.Id);
        // 📝 Disabled beds ride with zero coverage/thickness so the kernel skips them like the shader.
        if (!Bed.Enabled)
        {
            Bed.Coverage = 0;
            Bed.Thickness = 0;
        }
        Beds.push_back(Bed);
        ++Index;
    }
}

void SynthesizeSandBed(const MaterialRecipeParameters &Recipe, std::vector<MaterialSandBed> &Beds)
{
    // 📝 Single-grain sand builds its lone bed from the grain scalars, like the Alloy viewport.
    Beds.clear();
    const MaterialSandBed Defaults;
    MaterialSandBed Bed = Defaults;
    const auto Text = [&](const char *Key)
    {
        const auto Found = Recipe.Texts.find(Key);
        return Found != Recipe.Texts.end() ? std::optional<std::string>(Found->second) : std::nullopt;
    };
    const auto Color = Text("color"), Secondary = Text("secondaryColor");
    Bed.Color = CleanSandHex(Color.value_or(""), Color.has_value(), Defaults.Color);
    Bed.SecondaryColor = CleanSandHex(Secondary.value_or(""), Secondary.has_value(), Defaults.SecondaryColor);
    ClampSandBedNumerics(Bed,
        [&](const char *Key) -> std::optional<double>
        {
            // 📝 The lone type-37 bed has no thickness scalar; it always takes the unit default.
            const std::string_view BedKey(Key);
            if (BedKey == "thickness") return std::nullopt;
            const char *Scalar = BedKey == "size" ? "grainSize" : BedKey == "sizeVariation" ? "grainSizeVariation"
                : BedKey == "roughness"                                                     ? "grainRoughness"
                : BedKey == "roughnessVariation"                                            ? "grainRoughnessVariation"
                : BedKey == "specular"                                                      ? "grainSpecular"
                : BedKey == "specularVariation"                                             ? "grainSpecularVariation"
                : BedKey == "ior"                                                           ? "grainIOR"
                : BedKey == "roundness"                                                     ? "grainRoundness"
                : BedKey == "tilt"                                                          ? "grainTilt"
                : BedKey == "relief"                                                        ? "grainRelief"
                                                                                            : "sandCoverage";
            const auto Found = Recipe.Scalars.find(Scalar);
            return Found != Recipe.Scalars.end() ? std::optional<double>(Found->second) : std::nullopt;
        },
        Defaults);
    Beds.push_back(Bed);
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     CATALOGUE ENUMERATION
//------------------------------------------------------------------------------------------------------------------------

std::filesystem::path MaterialLibrary::ResolveMaterial(const std::filesystem::path &RelativePath) const
{
    if (Directory.empty() || RelativePath.empty() || RelativePath.is_absolute() || RelativePath.has_root_name() ||
        FoldMaterialText(EncodeContentPath(RelativePath.extension())) != ".material")
        throw std::runtime_error("Select a ContentDirectory-relative .material path.");
    const auto Text = EncodeContentPath(RelativePath);
    if (Text.find(':') != std::string::npos || Text.find('\\') != std::string::npos)
        throw std::runtime_error("Material paths must be portable relative paths.");
    const auto Resolved = std::filesystem::weakly_canonical(Directory/RelativePath);
    const auto Within = Resolved.lexically_relative(Directory);
    if (Within.empty() || *Within.begin() == "..") throw std::runtime_error("Material path escapes ContentDirectory.");
    return Resolved;
}

bool MaterialLibrary::ConfigureDirectory(const std::filesystem::path &Requested)
{
    DiagnosticJournal::Append("MaterialDirectory", DiagnosticJournal::EncodePath(Requested));
    try
    {
        if (!std::filesystem::is_directory(Requested)) throw std::runtime_error("Library directory does not exist.");
        MaterialLibrary Candidate;
        Candidate.Directory = std::filesystem::canonical(Requested);
        Candidate.Search = Search;
        if (!Candidate.Refresh()) throw std::runtime_error(Candidate.Diagnostic);
        Directory = std::move(Candidate.Directory);
        Entries = std::move(Candidate.Entries);
        Visible = std::move(Candidate.Visible);
        Truncated = Candidate.Truncated;
        Selection.clear();
        ParkedSources.clear();
        Source.clear();
        SavedSource.clear();
        AppliedSource.clear();
        Applied.reset();
        ++Revision;
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::Refresh()
{
    try
    {
        if (Directory.empty()) throw std::runtime_error("Open a material library directory first.");
        std::vector<MaterialCatalogueEntry> Replacement;
        size_t Visited = 0;
        bool Limited = false;
        for (std::filesystem::recursive_directory_iterator Position(Directory), End; Position != End; ++Position)
        {
            if (++Visited > 8192 || Replacement.size() >= 4096) { Limited = true; break; }
            if (Position.depth() >= 16) { Position.disable_recursion_pending(); Limited = true; }
            if (Position->is_symlink()) { Position.disable_recursion_pending(); continue; }
            if (!Position->is_regular_file() || FoldMaterialText(EncodeContentPath(Position->path().extension())) != ".material") continue;
            MaterialCatalogueEntry Entry;
            Entry.RelativePath = NormalizeMaterialPath(Position->path().lexically_relative(Directory));
            Entry.Name = EncodeContentPath(Entry.RelativePath.stem());
            try
            {
                const auto Material = DecodeSurfaceMaterial(DecodeMaterialSource(Position->path()));
                Entry.Name = Material.Name;
                Entry.StratumCount = Material.Scattering.Illumination.Configuration[0];
            }
            catch (const std::exception &Failure) { Entry.Diagnostic = Failure.what(); }
            Replacement.push_back(std::move(Entry));
        }
        // 📝 Optional catalogue metadata retains the HTML order without changing authored material syntax.
        const auto CataloguePath = Directory / "Library.toml";
        if (std::filesystem::is_regular_file(CataloguePath))
        {
            const auto Catalogue = toml::parse(DecodeMaterialSource(CataloguePath));
            if (Catalogue["Format"].value<int64_t>() != 1) throw std::runtime_error("Unsupported library catalogue format.");
            if (const auto *Materials = Catalogue["Materials"].as_array())
                for (const auto &MaterialTerm : *Materials)
                {
                    const toml::node_view<const toml::node> Material(&MaterialTerm);
                    const auto Path = Material["Path"].value<std::string>();
                    if (!Path) continue;
                    const auto Match = std::find_if(Replacement.begin(), Replacement.end(), [&](const auto &Entry)
                    { return EncodeContentPath(Entry.RelativePath) == *Path; });
                    if (Match == Replacement.end()) continue;
                    Match->CatalogueOrdinal = Material["Ordinal"].value_or(uint32_t(UINT32_MAX));
                    Match->Category = Material["Category"].value_or(std::string{});
                    Match->Label = Material["Label"].value_or(std::string{});
                    Match->Description = Material["Description"].value_or(std::string{});
                    Match->RecipeTitle = Material["RecipeTitle"].value_or(std::string{});
                    Match->RecipeCaption = Material["RecipeCaption"].value_or(std::string{});
                    Match->ColourHex = Material["Color"].value_or(std::string{});
                    if (const auto *Recipe = Material["SourceRecipe"].as_table())
                    {
                        const toml::node *SandLayersNode = nullptr;
                        for (const auto &[Key, Value] : *Recipe)
                        {
                            const std::string Name(Key.str());
                            if (Name == "recipeId")
                            {
                                if (const auto Id = Value.value<std::string>()) Match->Recipe.RecipeId = *Id;
                                continue;
                            }
                            if (Name == "sandLayers")
                            {
                                SandLayersNode = &Value;
                                continue;
                            }
                            if (Match->Recipe.Scalars.size() + Match->Recipe.Texts.size() >= 256) break;
                            if (Value.is_number() || Value.is_boolean())
                            {
                                const auto Scalar = Value.value<double>();
                                if (Scalar) Match->Recipe.Scalars[Name] = *Scalar;
                                else if (const auto Flag = Value.value<bool>()) Match->Recipe.Scalars[Name] = *Flag ? 1 : 0;
                            }
                            else if (const auto Text = Value.value<std::string>()) Match->Recipe.Texts[Name] = *Text;
                        }
                        // 📝 Sand beds normalize after the scalar pass: layered sand parses its bed tables,
                        // single-grain sand synthesizes its lone bed from the grain scalars.
                        const auto RecipeType = Match->Recipe.Scalars.find("type");
                        if (RecipeType != Match->Recipe.Scalars.end() && RecipeType->second == 37)
                            SynthesizeSandBed(Match->Recipe, Match->Recipe.SandBeds);
                        else if (RecipeType != Match->Recipe.Scalars.end() && RecipeType->second == 38)
                            ParseSandBeds(SandLayersNode, Match->Recipe.SandBeds);
                    }
                    if (const auto *Controls = Material["Controls"].as_array())
                        for (const auto &ControlTerm : *Controls)
                        {
                            const toml::node_view<const toml::node> Control(&ControlTerm);
                            if (Match->RecipeControls.size() >= 64) break;
                            Match->RecipeControls.push_back({Control["Id"].value_or(std::string{}),
                                Control["Caption"].value_or(std::string{}), Control["Group"].value_or(std::string{}),
                                float(Control["Position"].value_or(0.0)), float(Control["Minimum"].value_or(0.0)),
                                float(Control["Maximum"].value_or(1.0)), {}});
                            auto &RecipeControl = Match->RecipeControls.back();
                            RecipeControl.Direct = Control["Direct"].value_or(false);
                            RecipeControl.Logarithmic = Control["Logarithmic"].value_or(false);
                            RecipeControl.Step = float(Control["Step"].value_or(.01));
                            RecipeControl.Unit = Control["Unit"].value_or(std::string{});
                            if (const auto *Targets = Control["Targets"].as_array())
                                for (const auto &TargetTerm : *Targets)
                                {
                                    const toml::node_view<const toml::node> Target(&TargetTerm);
                                    if (Match->RecipeControls.back().Targets.size() >= 8) break;
                                    Match->RecipeControls.back().Targets.push_back({Target["Key"].value_or(std::string{}),
                                        float(Target["Minimum"].value_or(0.0)), float(Target["Maximum"].value_or(1.0))});
                                }
                        }
                }
        }
        std::sort(Replacement.begin(), Replacement.end(), [](const auto &Left, const auto &Right)
        {
            if (Left.CatalogueOrdinal != Right.CatalogueOrdinal) return Left.CatalogueOrdinal < Right.CatalogueOrdinal;
            return Left.RelativePath.generic_u8string() < Right.RelativePath.generic_u8string();
        });
        Entries = std::move(Replacement);
        for (auto Position = ParkedSources.begin(); Position != ParkedSources.end();)
        {
            const auto Present = std::find_if(Entries.begin(), Entries.end(),
                [&](const auto &Entry) { return Entry.RelativePath == Position->first; });
            if (Present == Entries.end()) Position = ParkedSources.erase(Position);
            else ++Position;
        }
        Truncated = Limited;
        FilterEntries();
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

void MaterialLibrary::ConfigureFilter(std::string Requested)
{
    std::replace(Requested.begin(), Requested.end(), '\\', '/');
    Search = FoldMaterialText(std::move(Requested));
    FilterEntries();
}

void MaterialLibrary::FilterEntries()
{
    Visible.clear();
    for (size_t Index = 0; Index < Entries.size(); ++Index)
    {
        // 📝 Search paths use portable separators and UTF-8, not the Windows-native backslash encoding.
        const auto RelativeText = Entries[Index].RelativePath.generic_u8string();
        const std::string RelativeName(RelativeText.begin(), RelativeText.end());
        if (Search.empty() || FoldMaterialText(Entries[Index].Name + " " + Entries[Index].Category + " " + RelativeName).find(Search) != std::string::npos)
            Visible.push_back(Index);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SOURCE TRANSACTIONS
//------------------------------------------------------------------------------------------------------------------------

bool MaterialLibrary::OpenMaterial(const std::filesystem::path &RelativePath)
{
    DiagnosticJournal::Append("MaterialOpen", DiagnosticJournal::EncodePath(RelativePath));
    try
    {
        const auto Target = NormalizeMaterialPath(RelativePath);
        auto ReplacementSource = DecodeMaterialSource(ResolveMaterial(RelativePath));
        auto Definition = DecodeSurfaceMaterial(ReplacementSource);
        EnsureProceduralChannelBakes(Definition, 256);
        auto Replacement = std::make_unique<CompiledSurfaceMaterial>(CompileSurfaceMaterial(Definition, Directory));
        // 📝 Unsaved drafts ride along across selections; reopening the same material is an explicit refresh from disk.
        const auto Previous = Selection;
        if (Target != Previous)
        {
            if (!Previous.empty() && Source != SavedSource) ParkedSources[Previous] = Source;
        }
        else ParkedSources.erase(Target);
        Selection = Target;
        Source = std::move(ReplacementSource);
        SavedSource = Source;
        AppliedSource = Source;
        ProceduralOverrides.clear();
        Applied = std::move(Replacement);
        if (Target != Previous)
        {
            const auto Parked = ParkedSources.find(Target);
            if (Parked != ParkedSources.end())
            {
                Source = Parked->second;
                ParkedSources.erase(Parked);
            }
        }
        ++Revision;
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

void MaterialLibrary::ReplaceSource(std::string Replacement)
{
    Source = std::move(Replacement);
    Diagnostic.clear();
}

bool MaterialLibrary::ConfigureStratumCoefficient(uint32_t Index, std::string_view Name, float Coefficient)
{
    if (Name != "Roughness" && Name != "Metallic" && Name != "Anisotropy" && Name != "Coverage" && Name != "Ior" && Name != "Specular" && Name != "HeightStrength" && Name != "NormalStrength" && Name != "Thickness" && Name != "SheenIntensity" && Name != "SheenRoughness")
    {
        Diagnostic = "Unsupported structured material coefficient.";
        return false;
    }
    char Number[64]{};
    const auto Encoded = std::to_chars(Number, Number + sizeof(Number), Coefficient);
    if (Encoded.ec != std::errc{}) { Diagnostic = "Material coefficient encoding failed."; return false; }
    std::string Numeric(Number, Encoded.ptr);
    if (Numeric.find_first_of(".eE") == std::string::npos) Numeric += ".0";
    return ConfigureStratumLiteral(Index, Name, Numeric);
}

bool MaterialLibrary::ConfigureStratumColour(uint32_t Index, const std::array<float, 3> &Spectrum, std::string_view Field)
{
    if (Field != "Albedo" && Field != "SheenColor")
    {
        Diagnostic = "Unsupported structured material colour.";
        return false;
    }
    std::string Numeric = "[";
    for (size_t Channel = 0; Channel < Spectrum.size(); ++Channel)
    {
        char Number[64]{};
        const auto Encoded = std::to_chars(Number, Number + sizeof(Number), Spectrum[Channel]);
        if (Encoded.ec != std::errc{}) { Diagnostic = "Material colour encoding failed."; return false; }
        if (Channel) Numeric += ", ";
        Numeric.append(Number, Encoded.ptr);
    }
    return ConfigureStratumLiteral(Index, Field, Numeric + "]");
}

bool MaterialLibrary::ConfigureStratumLiteral(uint32_t Index, std::string_view Name, const std::string &Numeric)
{
    try
    {
        auto Parsed = toml::parse(Source);
        auto *Strata = Parsed["Strata"].as_array();
        if (!Strata || Index >= Strata->size()) throw std::runtime_error("Material stratum does not exist.");
        auto *Stratum = Strata->get(Index)->as_table();
        if (!Stratum) throw std::runtime_error("Material stratum is not a table.");
        const auto Offset = [&](toml::source_position Position)
        {
            size_t Cursor = 0;
            for (uint32_t Line = 1; Line < Position.line; ++Line)
            {
                Cursor = Source.find('\n', Cursor);
                if (Cursor == std::string::npos) throw std::runtime_error("Invalid material source coordinates.");
                ++Cursor;
            }
            for (uint32_t Column = 1; Column < Position.column; ++Column)
            {
                if (Cursor >= Source.size()) throw std::runtime_error("Invalid material source column.");
                ++Cursor;
                while (Cursor < Source.size() && (static_cast<unsigned char>(Source[Cursor]) & 0xc0) == 0x80) ++Cursor;
            }
            return Cursor;
        };
        auto Replacement = Source;
        if (const auto *Existing = Stratum->get(Name))
        {
            const auto Region = Existing->source();
            const auto Begin = Offset(Region.begin), End = Offset(Region.end);
            Replacement.replace(Begin, End - Begin, Numeric);
        }
        else
        {
            const auto Begin = Offset(Stratum->source().begin);
            if (Begin < Source.size() && Source[Begin] == '{')
                Replacement.insert(Begin + 1, std::string(Name) + " = " + Numeric + (Stratum->empty() ? "" : ", "));
            else
            {
                const auto Header = Source.rfind("[[Strata]]", Begin);
                if (Header == std::string::npos) throw std::runtime_error("Add this coefficient in Source view first.");
                const auto End = Source.find('\n', Header);
                if (End == std::string::npos) Replacement += "\n" + std::string(Name) + " = " + Numeric + "\n";
                else Replacement.insert(End + 1, std::string(Name) + " = " + Numeric + (End && Source[End - 1] == '\r' ? "\r\n" : "\n"));
            }
        }
        (void)DecodeSurfaceMaterial(Replacement);
        Source = std::move(Replacement);
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::InsertStratum(uint32_t Index)
{
    try
    {
        if (Selection.empty()) throw std::runtime_error("Select a material first.");
        const auto Blocks = SplitStrataBlocks(Source);
        if (Index > Blocks.size()) throw std::runtime_error("Material stratum does not exist.");
        if (Blocks.size() >= 8) throw std::runtime_error("Material holds the maximum 8 strata.");
        if (Blocks.empty()) throw std::runtime_error("Add a [[Strata]] table in Source view first.");
        static constexpr const char *Template =
            "[[Strata]]\nCategory = \"Coating\"\nAlbedo = [1.0, 1.0, 1.0]\nRoughness = 0.15\nMetallic = 0.0\nIor = 1.5\nCoverage = 0.65\n";
        const size_t At = Index < Blocks.size() ? Blocks[Index].Begin : Blocks.back().End;
        std::string Head = Source.substr(0, At), Tail = Source.substr(At);
        if (!Head.empty() && Head.back() != '\n') Head += '\n';
        const std::string Replacement = Head + Template + Tail;
        (void)DecodeSurfaceMaterial(Replacement);
        Source = Replacement;
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::RemoveStratum(uint32_t Index)
{
    try
    {
        if (Selection.empty()) throw std::runtime_error("Select a material first.");
        const auto Blocks = SplitStrataBlocks(Source);
        if (Index >= Blocks.size()) throw std::runtime_error("Material stratum does not exist.");
        if (Blocks.size() < 2) throw std::runtime_error("Material requires at least one stratum.");
        auto Replacement = Source;
        Replacement.erase(Blocks[Index].Begin, Blocks[Index].End - Blocks[Index].Begin);
        (void)DecodeSurfaceMaterial(Replacement);
        Source = std::move(Replacement);
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::ConfigureStratumChannel(uint32_t Index, std::string_view Channel, std::string_view Op)
{
    try
    {
        if (Selection.empty()) throw std::runtime_error("Select a material first.");
        const std::string Name(Channel), Operation(Op);
        if (Name != "Albedo" && Name != "Orm" && Name != "Normal" && Name != "Radiance")
            throw std::runtime_error("Unknown stratum channel.");
        if (!Operation.empty())
        {
            const auto Ops = QueryProceduralOps();
            if (std::find(Ops.begin(), Ops.end(), Operation) == Ops.end()) throw std::runtime_error("Unknown procedural op.");
            const auto Supported = QueryProceduralOpChannels(Operation);
            if (std::find(Supported.begin(), Supported.end(), Name) == Supported.end())
                throw std::runtime_error("Procedural op does not bake the requested channel.");
        }
        auto Blocks = SplitStrataBlocks(Source);
        if (Index >= Blocks.size()) throw std::runtime_error("Material stratum does not exist.");
        auto Definition = DecodeSurfaceMaterial(Source);
        const MaterialProceduralChannel *Existing = nullptr;
        for (const auto &Entry : Definition.StratumChannels[Index])
            if (Entry.Name == Name) Existing = &Entry;
        auto Replacement = Source;
        const auto Refresh = [&]() { Blocks = SplitStrataBlocks(Replacement); };
        // Same-op switches keep params and links; anything else rebuilds the sub-block from op defaults.
        const bool Keep = Existing && Existing->Op == Operation;
        if (!Keep)
        {
            Refresh();
            for (const auto &Span : SplitChannelBlocks(Replacement, Blocks[Index]))
                if (Span.Name == Name) Replacement.erase(Span.Span.Begin, Span.Span.End - Span.Span.Begin);
            if (!Operation.empty())
            {
                std::map<std::string, double> Seeds;
                for (const auto &Entry : Entries)
                    if (Entry.RelativePath == Selection) Seeds = Entry.Recipe.Scalars;
                std::string Block = "[[Strata.Channels]]\nName = \"" + Name + "\"\nOp = \"" + Operation + "\"\nLink = \"shared\"\n[Strata.Channels.Params]\n";
                for (const auto &Param : QueryProceduralOpParams(Operation))
                {
                    double Value = Param.Default;
                    const auto Seeded = Seeds.find(Param.Key);
                    if (Seeded != Seeds.end()) Value = Seeded->second;
                    else if (std::string_view(Param.Key) == "angle")
                    {
                        const auto Weave = Seeds.find("weaveAngle"), Scratch = Seeds.find("scratchAngle");
                        if (Weave != Seeds.end()) Value = Weave->second;
                        else if (Scratch != Seeds.end()) Value = Scratch->second;
                    }
                    Block += std::string(Param.Key) + " = " + EncodeSourceDouble(Value) + "\n";
                }
                Refresh();
                const size_t At = Blocks[Index].End;
                std::string Head = Replacement.substr(0, At), Tail = Replacement.substr(At);
                if (!Head.empty() && Head.back() != '\n') Head += '\n';
                Replacement = Head + Block + Tail;
            }
        }
        // TextureMask follows the channel switch; procedural takes over any file source for the slot.
        Refresh();
        const uint32_t Bit = Name == "Albedo" ? 1 : Name == "Orm" ? 2 : Name == "Normal" ? 4 : 8;
        uint32_t Mask = DecodeSurfaceMaterial(Replacement).Scattering.TextureUse[Index][0];
        if (Operation.empty()) Mask &= ~Bit;
        else Mask |= Bit;
        SourceSpan Head{Blocks[Index].Begin, Blocks[Index].End};
        size_t Cursor = Blocks[Index].Begin;
        while (Cursor < Blocks[Index].End)
        {
            const size_t Stop = Replacement.find('\n', Cursor);
            const size_t End = Stop == std::string::npos || Stop > Blocks[Index].End ? Blocks[Index].End : Stop;
            const auto Trimmed = TrimSourceLine(std::string_view(Replacement.data() + Cursor, End - Cursor));
            if (Cursor > Blocks[Index].Begin && Trimmed.starts_with("[")) { Head.End = Cursor; break; }
            if (Stop == std::string::npos || Stop >= Blocks[Index].End) break;
            Cursor = Stop + 1;
        }
        const auto MaskLine = FindKeyLine(Replacement, Head, "TextureMask");
        const std::string MaskText = "TextureMask = " + std::to_string(Mask) + "\n";
        if (MaskLine.End > MaskLine.Begin) Replacement.replace(MaskLine.Begin, MaskLine.End - MaskLine.Begin, MaskText);
        else
        {
            const size_t Header = Replacement.find('\n', Blocks[Index].Begin);
            if (Header == std::string::npos || Header >= Blocks[Index].End) throw std::runtime_error("Stratum header is malformed.");
            Replacement.insert(Header + 1, MaskText);
        }
        Refresh();
        const auto Textures = FindTexturesSection(Replacement, Blocks[Index]);
        if (Textures.End > Textures.Begin)
        {
            const auto SourceLine = FindKeyLine(Replacement, Textures, Name);
            if (SourceLine.End > SourceLine.Begin) Replacement.erase(SourceLine.Begin, SourceLine.End - SourceLine.Begin);
        }
        (void)DecodeSurfaceMaterial(Replacement);
        Source = std::move(Replacement);
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::ForkProceduralChannel(uint32_t Index, std::string_view Channel)
{
    try
    {
        if (Selection.empty()) throw std::runtime_error("Select a material first.");
        const auto Definition = DecodeSurfaceMaterial(Source);
        const MaterialProceduralChannel *Target = nullptr;
        for (const auto &Entry : Definition.StratumChannels[Index])
            if (Entry.Name == Channel) Target = &Entry;
        if (!Target || Target->Op.empty()) throw std::runtime_error("Channel has no procedural pool to fork.");
        uint32_t Fork = 1;
        for (const auto &Stratum : Definition.StratumChannels)
            for (const auto &Entry : Stratum)
                if (Entry.Link.starts_with("fork"))
                {
                    uint32_t Suffix = 0;
                    const auto [Last, Error] = std::from_chars(Entry.Link.data() + 4, Entry.Link.data() + Entry.Link.size(), Suffix);
                    if (Error == std::errc{} && Last == Entry.Link.data() + Entry.Link.size()) Fork = std::max(Fork, Suffix + 1);
                }
        const auto Blocks = SplitStrataBlocks(Source);
        if (Index >= Blocks.size()) throw std::runtime_error("Material stratum does not exist.");
        auto Replacement = Source;
        bool Forked = false;
        for (const auto &Span : SplitChannelBlocks(Replacement, Blocks[Index]))
        {
            if (Span.Name != Channel) continue;
            const auto LinkLine = FindKeyLine(Replacement, Span.Span, "Link");
            const std::string LinkText = "Link = \"fork" + std::to_string(Fork) + "\"\n";
            if (LinkLine.End > LinkLine.Begin) Replacement.replace(LinkLine.Begin, LinkLine.End - LinkLine.Begin, LinkText);
            else
            {
                const size_t Header = Replacement.find('\n', Span.Span.Begin);
                if (Header == std::string::npos || Header >= Span.Span.End) throw std::runtime_error("Channel header is malformed.");
                Replacement.insert(Header + 1, LinkText);
            }
            Forked = true;
        }
        if (!Forked) throw std::runtime_error("Channel source block is missing.");
        (void)DecodeSurfaceMaterial(Replacement);
        Source = std::move(Replacement);
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::MergeProceduralChannel(uint32_t Index, std::string_view Channel, uint32_t SourceStratum, std::string_view SourceChannel)
{
    try
    {
        if (Selection.empty()) throw std::runtime_error("Select a material first.");
        const std::string Name(Channel);
        if (Name != "Albedo" && Name != "Orm" && Name != "Normal" && Name != "Radiance")
            throw std::runtime_error("Unknown stratum channel.");
        const auto Definition = DecodeSurfaceMaterial(Source);
        if (SourceStratum >= Definition.StratumChannels.size()) throw std::runtime_error("Merge source stratum does not exist.");
        const MaterialProceduralChannel *Donor = nullptr;
        for (const auto &Entry : Definition.StratumChannels[SourceStratum])
            if (Entry.Name == SourceChannel) Donor = &Entry;
        if (!Donor || Donor->Op.empty()) throw std::runtime_error("Merge source is not procedural.");
        const auto Supported = QueryProceduralOpChannels(Donor->Op);
        if (std::find(Supported.begin(), Supported.end(), Name) == Supported.end())
            throw std::runtime_error("Procedural op does not bake the requested channel.");
        auto Blocks = SplitStrataBlocks(Source);
        if (Index >= Blocks.size()) throw std::runtime_error("Material stratum does not exist.");
        auto Replacement = Source;
        for (const auto &Span : SplitChannelBlocks(Replacement, Blocks[Index]))
            if (Span.Name == Name) Replacement.erase(Span.Span.Begin, Span.Span.End - Span.Span.Begin);
        const auto Escape = [](const std::string &Text)
        {
            std::string Escaped;
            for (char Byte : Text)
            {
                if (Byte == '"' || Byte == '\\') Escaped += '\\';
                Escaped += Byte;
            }
            return Escaped;
        };
        std::string Block = "[[Strata.Channels]]\nName = \"" + Name + "\"\nOp = \"" + Escape(Donor->Op) + "\"\nLink = \"" +
            Escape(Donor->Link) + "\"\n[Strata.Channels.Params]\n";
        for (const auto &[Key, Value] : Donor->Params) Block += Key + " = " + EncodeSourceDouble(Value) + "\n";
        for (const auto &[Key, Value] : Donor->Texts) Block += Key + " = \"" + Escape(Value) + "\"\n";
        Blocks = SplitStrataBlocks(Replacement);
        const size_t At = Blocks[Index].End;
        std::string Head = Replacement.substr(0, At), Tail = Replacement.substr(At);
        if (!Head.empty() && Head.back() != '\n') Head += '\n';
        Replacement = Head + Block + Tail;
        (void)DecodeSurfaceMaterial(Replacement);
        Source = std::move(Replacement);
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::ConfigureProceduralParam(uint32_t Index, std::string_view Channel, std::string_view Key, double Value)
{
    try
    {
        if (Selection.empty()) throw std::runtime_error("Select a material first.");
        if (!std::isfinite(Value)) throw std::runtime_error("Procedural param is not finite.");
        const auto Definition = DecodeSurfaceMaterial(Source);
        if (Index >= Definition.StratumChannels.size()) throw std::runtime_error("Material stratum does not exist.");
        const MaterialProceduralChannel *Target = nullptr;
        for (const auto &Entry : Definition.StratumChannels[Index])
            if (Entry.Name == Channel) Target = &Entry;
        if (!Target || Target->Op.empty()) throw std::runtime_error("Channel is not procedural.");
        const std::string Canonical = ResolveProceduralParamKey(Target->Op, Key);
        struct Linked
        {
            uint32_t Stratum = 0;
            std::string Name;
        };
        std::vector<Linked> Pool;
        for (size_t Stratum = 0; Stratum < Definition.StratumChannels.size(); ++Stratum)
            for (const auto &Entry : Definition.StratumChannels[Stratum])
                if (!Entry.Op.empty() && Entry.Op == Target->Op && Entry.Link == Target->Link)
                    Pool.push_back({uint32_t(Stratum), Entry.Name});
        if (Pool.empty()) throw std::runtime_error("Procedural pool entry is missing.");
        auto Replacement = Source;
        for (const auto &Member : Pool)
        {
            const auto Blocks = SplitStrataBlocks(Replacement);
            if (Member.Stratum >= Blocks.size()) throw std::runtime_error("Procedural pool stratum is missing.");
            bool Edited = false;
            for (const auto &Span : SplitChannelBlocks(Replacement, Blocks[Member.Stratum]))
            {
                if (Span.Name != Member.Name) continue;
                const auto Inline = FindKeyLine(Replacement, Span.Span, "Params");
                if (Inline.End > Inline.Begin) throw std::runtime_error("Expand inline Params in Source view first.");
                SourceSpan Params{0, 0};
                size_t Cursor = Span.Span.Begin;
                while (Cursor < Span.Span.End)
                {
                    const size_t Stop = Replacement.find('\n', Cursor);
                    const size_t End = Stop == std::string::npos || Stop > Span.Span.End ? Span.Span.End : Stop;
                    if (IsChannelParamsHeader(TrimSourceLine(std::string_view(Replacement.data() + Cursor, End - Cursor))))
                    {
                        Params = {Cursor, Span.Span.End};
                        break;
                    }
                    if (Stop == std::string::npos || Stop >= Span.Span.End) break;
                    Cursor = Stop + 1;
                }
                const std::string Line = Canonical + " = " + EncodeSourceDouble(Value) + "\n";
                if (Params.End > Params.Begin)
                {
                    const auto Existing = FindKeyLine(Replacement, Params, Canonical);
                    if (Existing.End > Existing.Begin) Replacement.replace(Existing.Begin, Existing.End - Existing.Begin, Line);
                    else
                    {
                        std::string Head = Replacement.substr(0, Span.Span.End), Tail = Replacement.substr(Span.Span.End);
                        if (!Head.empty() && Head.back() != '\n') Head += '\n';
                        Replacement = Head + Line + Tail;
                    }
                }
                else
                {
                    std::string Head = Replacement.substr(0, Span.Span.End), Tail = Replacement.substr(Span.Span.End);
                    if (!Head.empty() && Head.back() != '\n') Head += '\n';
                    Replacement = Head + "[Strata.Channels.Params]\n" + Line + Tail;
                }
                Edited = true;
            }
            if (!Edited) throw std::runtime_error("Procedural pool block is missing.");
        }
        (void)DecodeSurfaceMaterial(Replacement);
        Source = std::move(Replacement);
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

// 📝 Text twin of ConfigureProceduralParam: palette colours ride the same pool/source-line machinery so a
// shared coded field recolours every linked channel with one edit and authored comments survive.
bool MaterialLibrary::ConfigureProceduralText(uint32_t Index, std::string_view Channel, std::string_view Key, std::string_view Text)
{
    try
    {
        if (Selection.empty()) throw std::runtime_error("Select a material first.");
        if (Text.empty() || Text.size() > 1024 || Text.find_first_of("\n\r\"\\") != std::string_view::npos)
            throw std::runtime_error("Procedural text is empty, unbounded or unquotable.");
        const auto Definition = DecodeSurfaceMaterial(Source);
        if (Index >= Definition.StratumChannels.size()) throw std::runtime_error("Material stratum does not exist.");
        const MaterialProceduralChannel *Target = nullptr;
        for (const auto &Entry : Definition.StratumChannels[Index])
            if (Entry.Name == Channel) Target = &Entry;
        if (!Target || Target->Op.empty()) throw std::runtime_error("Channel is not procedural.");
        const std::string Canonical(Key);
        struct Linked
        {
            uint32_t Stratum = 0;
            std::string Name;
        };
        std::vector<Linked> Pool;
        for (size_t Stratum = 0; Stratum < Definition.StratumChannels.size(); ++Stratum)
            for (const auto &Entry : Definition.StratumChannels[Stratum])
                if (!Entry.Op.empty() && Entry.Op == Target->Op && Entry.Link == Target->Link)
                    Pool.push_back({uint32_t(Stratum), Entry.Name});
        if (Pool.empty()) throw std::runtime_error("Procedural pool entry is missing.");
        auto Replacement = Source;
        for (const auto &Member : Pool)
        {
            const auto Blocks = SplitStrataBlocks(Replacement);
            if (Member.Stratum >= Blocks.size()) throw std::runtime_error("Procedural pool stratum is missing.");
            bool Edited = false;
            for (const auto &Span : SplitChannelBlocks(Replacement, Blocks[Member.Stratum]))
            {
                if (Span.Name != Member.Name) continue;
                const auto Inline = FindKeyLine(Replacement, Span.Span, "Params");
                if (Inline.End > Inline.Begin) throw std::runtime_error("Expand inline Params in Source view first.");
                SourceSpan Params{0, 0};
                size_t Cursor = Span.Span.Begin;
                while (Cursor < Span.Span.End)
                {
                    const size_t Stop = Replacement.find('\n', Cursor);
                    const size_t End = Stop == std::string::npos || Stop > Span.Span.End ? Span.Span.End : Stop;
                    if (IsChannelParamsHeader(TrimSourceLine(std::string_view(Replacement.data() + Cursor, End - Cursor))))
                    {
                        Params = {Cursor, Span.Span.End};
                        break;
                    }
                    if (Stop == std::string::npos || Stop >= Span.Span.End) break;
                    Cursor = Stop + 1;
                }
                const std::string Line = Canonical + " = \"" + std::string(Text) + "\"\n";
                if (Params.End > Params.Begin)
                {
                    const auto Existing = FindKeyLine(Replacement, Params, Canonical);
                    if (Existing.End > Existing.Begin) Replacement.replace(Existing.Begin, Existing.End - Existing.Begin, Line);
                    else
                    {
                        std::string Head = Replacement.substr(0, Span.Span.End), Tail = Replacement.substr(Span.Span.End);
                        if (!Head.empty() && Head.back() != '\n') Head += '\n';
                        Replacement = Head + Line + Tail;
                    }
                }
                else
                {
                    std::string Head = Replacement.substr(0, Span.Span.End), Tail = Replacement.substr(Span.Span.End);
                    if (!Head.empty() && Head.back() != '\n') Head += '\n';
                    Replacement = Head + "[Strata.Channels.Params]\n" + Line + Tail;
                }
                Edited = true;
            }
            if (!Edited) throw std::runtime_error("Procedural pool block is missing.");
        }
        (void)DecodeSurfaceMaterial(Replacement);
        Source = std::move(Replacement);
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::RebakeProcedurals(uint32_t Extent)
{
    try
    {
        if (Selection.empty()) throw std::runtime_error("Select a material first.");
        if (!Extent || Extent > 1024) throw std::runtime_error("Rebake extent is unbounded.");
        const auto Definition = DecodeSurfaceMaterial(Source);
        std::map<std::pair<size_t, std::string>, SurfaceTextureImage> Baked;
        const size_t Count = Definition.Scattering.Illumination.Configuration[0];
        for (size_t Index = 0; Index < Count; ++Index)
            for (const auto &Entry : Definition.StratumChannels[Index])
            {
                if (Entry.Op.empty()) continue;
                const size_t Slot =
                    Entry.Name == "Albedo" ? 0 : Entry.Name == "Orm" ? 1 : Entry.Name == "Normal" ? 2 : 3;
                const std::string &Key = Definition.StratumTextureSources[Index][Slot];
                auto Rgba = BakeProceduralChannelRgba(Entry.Op, Entry.Name, Entry.Params, Entry.Texts, Extent);
                SurfaceTextureImage BakedImage{Extent, Extent, std::move(Rgba)};
                // 📝 Key by the live source for compile lookup and by the derived key for the save path, so a
                // re-save after param tweaks refreshes the persisted PNGs instead of skipping them as files.
                Baked[{Slot, EncodeProceduralSource(Index, Entry.Name)}] = BakedImage;
                Baked[{Slot, Key}] = std::move(BakedImage);
            }
        if (Baked.empty()) throw std::runtime_error("No procedural channels to rebake.");
        ProceduralOverrides = std::move(Baked);
        return ApplySource();
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::PersistProceduralOverrides()
{
    try
    {
        if (Selection.empty() || ProceduralOverrides.empty()) return true;
        const std::string Stem = EncodeContentPath(Selection.stem());
        auto Replacement = Source;
        for (const auto &[SlotKey, Image] : ProceduralOverrides)
        {
            const auto &[Slot, Key] = SlotKey;
            if (!Key.starts_with("procedural/")) continue;
            const auto Middle = Key.find('/', 11), NameBegin = Middle == std::string::npos ? std::string::npos : Middle + 1;
            if (NameBegin == std::string::npos) continue;
            size_t Index = 0;
            const auto [Last, Error] =
                std::from_chars(Key.data() + 11, Key.data() + Middle, Index);
            if (Error != std::errc{} || Last != Key.data() + Middle) continue;
            const std::string Lower(Key.substr(NameBegin));
            const std::string Name = Lower == "albedo" ? "Albedo" : Lower == "orm" ? "Orm" : Lower == "normal" ? "Normal" :
                Lower == "radiance" ? "Radiance" : "";
            if (Name.empty() || Slot > 3) continue;
            const std::string File = "Textures/" + Stem + "-" + Lower + ".png";
            const auto Png = EncodeProceduralPng(Image.Rgba, Image.Width);
            const auto Destination = Directory / File;
            std::filesystem::create_directories(Destination.parent_path());
            std::ofstream Output(Destination, std::ios::binary | std::ios::trunc);
            Output.write(reinterpret_cast<const char *>(Png.data()), Png.size());
            if (!Output) throw std::runtime_error("Procedural texture write failed.");
            auto Blocks = SplitStrataBlocks(Replacement);
            if (Index >= Blocks.size()) throw std::runtime_error("Procedural stratum is missing.");
            auto Textures = FindTexturesSection(Replacement, Blocks[Index]);
            const std::string Line = Name + " = \"" + File + "\"\n";
            if (Textures.End > Textures.Begin)
            {
                const auto Existing = FindKeyLine(Replacement, Textures, Name);
                if (Existing.End > Existing.Begin) Replacement.replace(Existing.Begin, Existing.End - Existing.Begin, Line);
                else
                {
                    std::string Head = Replacement.substr(0, Textures.End), Tail = Replacement.substr(Textures.End);
                    if (!Head.empty() && Head.back() != '\n') Head += '\n';
                    Replacement = Head + Line + Tail;
                }
            }
            else
            {
                Blocks = SplitStrataBlocks(Replacement);
                std::string Head = Replacement.substr(0, Blocks[Index].End), Tail = Replacement.substr(Blocks[Index].End);
                if (!Head.empty() && Head.back() != '\n') Head += '\n';
                Replacement = Head + "[Strata.Textures]\n" + Line + Tail;
            }
        }
        (void)DecodeSurfaceMaterial(Replacement);
        Source = std::move(Replacement);
        ProceduralOverrides.clear();
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::ConfigurePaint(const AutomotivePaint &Paint)
{
    try
    {
        auto Definition = DecodeSurfaceMaterial(Source);
        Definition.Scattering.Paint = Paint;
        const auto Encoded = toml::parse(EncodeSurfaceMaterial(Definition));
        const auto Original = toml::parse(Source);
        const auto *PaintTable = Original["AutomotivePaint"].as_table();
        const auto *ReplacementTable = Encoded["AutomotivePaint"].as_table();
        if (!PaintTable || !ReplacementTable) throw std::runtime_error("Add an AutomotivePaint table in Source first.");
        const auto Offset = [&](toml::source_position Position)
        {
            size_t Cursor = 0;
            for (uint32_t Line = 1; Line < Position.line; ++Line)
            {
                Cursor = Source.find('\n', Cursor);
                if (Cursor == std::string::npos) throw std::runtime_error("Invalid paint source line.");
                ++Cursor;
            }
            for (uint32_t Column = 1; Column < Position.column; ++Column)
            {
                if (Cursor >= Source.size()) throw std::runtime_error("Invalid paint source column.");
                ++Cursor;
                while (Cursor < Source.size() && (static_cast<unsigned char>(Source[Cursor]) & 0xc0) == 0x80) ++Cursor;
            }
            return Cursor;
        };
        struct PaintReplacement { size_t Begin, End; std::string Literal; };
        std::vector<PaintReplacement> Replacements;
        for (const auto &[Key, Coefficient] : *ReplacementTable)
        {
            const auto *Existing = PaintTable->get(Key.str());
            if (!Existing) throw std::runtime_error("Add the missing paint coefficient in Source first.");
            std::ostringstream Literal;
            Literal << toml::toml_formatter(Coefficient);
            Replacements.push_back({Offset(Existing->source().begin), Offset(Existing->source().end), Literal.str()});
        }
        std::sort(Replacements.begin(), Replacements.end(), [](const auto &First, const auto &Second) { return First.Begin > Second.Begin; });
        auto Replacement = Source;
        for (const auto &Term : Replacements) Replacement.replace(Term.Begin, Term.End - Term.Begin, Term.Literal);
        (void)DecodeSurfaceMaterial(Replacement);
        Source = std::move(Replacement);
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::ConfigureEmissionStrength(float Strength)
{
    try
    {
        if (!(Strength >= 0) || Strength > 64) throw std::runtime_error("Emission strength is out of range.");
        auto Definition = DecodeSurfaceMaterial(Source);
        auto &Radiance = Definition.Scattering.RadianceNormal;
        const float Peak = std::max({Radiance[0], Radiance[1], Radiance[2]});
        if (!(Peak > 0))
        {
            if (Strength == 0) return true;
            throw std::runtime_error("Author an emission colour in Source first.");
        }
        const float Scale = Strength / Peak;
        Radiance[0] *= Scale;
        Radiance[1] *= Scale;
        Radiance[2] *= Scale;
        const auto Encoded = toml::parse(EncodeSurfaceMaterial(Definition));
        const auto Original = toml::parse(Source);
        const auto *Existing = Original["Radiance"].as_array();
        const auto *ReplacementValue = Encoded["Radiance"].as_array();
        if (!Existing || !ReplacementValue) throw std::runtime_error("Add a Radiance spectrum in Source first.");
        const auto Offset = [&](toml::source_position Position)
        {
            size_t Cursor = 0;
            for (uint32_t Line = 1; Line < Position.line; ++Line)
            {
                Cursor = Source.find('\n', Cursor);
                if (Cursor == std::string::npos) throw std::runtime_error("Invalid radiance source line.");
                ++Cursor;
            }
            for (uint32_t Column = 1; Column < Position.column; ++Column)
            {
                if (Cursor >= Source.size()) throw std::runtime_error("Invalid radiance source column.");
                ++Cursor;
                while (Cursor < Source.size() && (static_cast<unsigned char>(Source[Cursor]) & 0xc0) == 0x80) ++Cursor;
            }
            return Cursor;
        };
        std::ostringstream Literal;
        Literal << std::setprecision(10) << "[" << Radiance[0] << ", " << Radiance[1] << ", " << Radiance[2] << "]";
        auto Replacement = Source;
        Replacement.replace(Offset(Existing->source().begin),
            Offset(Existing->source().end) - Offset(Existing->source().begin), Literal.str());
        (void)DecodeSurfaceMaterial(Replacement);
        Source = std::move(Replacement);
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::ApplySource()
{
    DiagnosticJournal::Append("MaterialOperation", "ApplySource " + DiagnosticJournal::EncodePath(Selection) + " revision=" + std::to_string(Revision));
    try
    {
        if (Selection.empty()) throw std::runtime_error("Select a material first.");
        auto Definition = DecodeSurfaceMaterial(Source);
        Definition.ProceduralMaps = ProceduralOverrides;
        if (ProceduralOverrides.empty()) EnsureProceduralChannelBakes(Definition, 256);
        auto Replacement = std::make_unique<CompiledSurfaceMaterial>(CompileSurfaceMaterial(Definition, Directory));
        Applied = std::move(Replacement);
        AppliedSource = Source;
        ++Revision;
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::RevertSource()
{
    DiagnosticJournal::Append("MaterialOperation", "RevertSource " + DiagnosticJournal::EncodePath(Selection) + " revision=" + std::to_string(Revision));
    const auto Previous = Source;
    Source = SavedSource;
    ProceduralOverrides.clear();
    if (ApplySource())
    {
        ParkedSources.erase(Selection);
        return true;
    }
    Source = Previous;
    return false;
}

bool MaterialLibrary::QueryParked(const std::filesystem::path &RelativePath) const
{
    return ParkedSources.find(NormalizeMaterialPath(RelativePath)) != ParkedSources.end();
}

bool MaterialLibrary::SaveMaterial()
{
    DiagnosticJournal::Append("MaterialOperation", "SaveMaterial " + DiagnosticJournal::EncodePath(Selection) + " revision=" + std::to_string(Revision));
    try
    {
        if (Selection.empty()) throw std::runtime_error("Select a material first.");
        const auto Destination = ResolveMaterial(Selection);
        if (DecodeMaterialSource(Destination) != SavedSource)
            throw std::runtime_error("Material changed on disk; reopen it before saving to avoid overwriting external edits.");
        // 📝 Live rebakes already preview at committed-cache resolution; saving re-bakes to capture the
        // latest params, persists the PNGs and rewrites the stratum sources to the files, so the saved
        // material stands alone. Any retained op triggers the rebake — even one that has never been
        // baked — so a channel switch alone still saves.
        const auto PreSave = DecodeSurfaceMaterial(Source);
        bool Procedural = false;
        for (const auto &Stratum : PreSave.StratumChannels)
            for (const auto &Entry : Stratum) Procedural = Procedural || !Entry.Op.empty();
        if (Procedural && !RebakeProcedurals(512)) return false;
        if (!ProceduralOverrides.empty() && !PersistProceduralOverrides()) return false;
        if (QueryUnapplied() && !ApplySource()) return false;
        if (!TextFileReplacement::Finalize(Destination, Source, Diagnostic)) return false;
        SavedSource = Source;
        ParkedSources.erase(Selection);
        const auto Saved = Source;
        if (!Refresh()) return false;
        Source = Saved;
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::CreateMaterial(const std::filesystem::path &RelativePath, const std::string &Name)
{
    try
    {
        MaterialDefinition Definition;
        Definition.Name = Name;
        const auto Replacement = EncodeSurfaceMaterial(Definition);
        const auto Destination = ResolveMaterial(RelativePath);
        // 📝 Folder creation is explicit in the directory picker; creating a material never creates arbitrary folder trees.
        if (!std::filesystem::is_directory(Destination.parent_path())) throw std::runtime_error("Destination folder does not exist.");
        CreateMaterialSource(Destination, Replacement);
        return Refresh() && OpenMaterial(RelativePath);
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::DuplicateMaterial(const std::filesystem::path &RelativePath)
{
    try
    {
        if (Selection.empty()) throw std::runtime_error("Select a material first.");
        const auto Destination = ResolveMaterial(RelativePath);
        // 📝 Duplicate the currently authored source, but never write an invalid/uncompilable draft.
        (void)CompileSurfaceMaterial(DecodeSurfaceMaterial(Source), Directory);
        CreateMaterialSource(Destination, Source);
        return Refresh();
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}
} // namespace Frontier
