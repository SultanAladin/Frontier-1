/*==========================================================================================================================================
                                                            MATERIALLIBRARY.H
==========================================================================================================================================*/
// 🧩 Root-contained material catalogue and source/apply/save transactions, independent of any application or UI.

#pragma once
#include "Engine/Render/SurfaceMaterial.h"
#include <map>
#include <memory>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     MATERIAL CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

struct MaterialRecipeTarget
{
    std::string Key;
    float Minimum = 0, Maximum = 1;
    // 📝 Integer targets round their projected value (HTML ProjectDesignerControl), e.g. thread density.
    bool Integer = false;
};

struct MaterialRecipeControl
{
    std::string Id, Caption, Group;
    float Position = 0, Minimum = 0, Maximum = 1;
    std::vector<MaterialRecipeTarget> Targets;
    bool Direct = false, Logarithmic = false;
    float Step = .01f;
    std::string Unit;
    // 📝 Direct controls carry their own address; macro controls project through Targets only.
    std::string Key;
    // 📝 Scale controls extend their domain to whatever the material currently holds, and hints are the
    // HTML recipe's per-control guidance text.
    bool Extend = false;
    std::string Hint;
};

// 📝 Parsed Alloy SourceRecipe: the uniform values the baker and the procedural controls share. Scalars and
// texts cover every shipped control; sandLayers additionally normalizes into bed tables for the sand baker.
struct MaterialSandBed
{
    std::string Id = "sand-bed-1";
    bool Enabled = true;
    std::string Color = "#c9aa79", SecondaryColor = "#eee2c7";
    double Size = .65, SizeVariation = .5;
    double Roughness = .6, RoughnessVariation = .3, Specular = .8, SpecularVariation = .35;
    double Ior = 1.544, Roundness = .45, Tilt = .32, Relief = .85, Coverage = 1, Thickness = 1;
};

struct MaterialRecipeParameters
{
    std::string RecipeId;
    std::map<std::string, double> Scalars;
    std::map<std::string, std::string> Texts;
    std::vector<MaterialSandBed> SandBeds;
};

struct MaterialCatalogueEntry
{
    std::filesystem::path RelativePath;
    std::string           Name;
    std::string           Diagnostic;
    uint32_t              StratumCount = 0;
    uint32_t              CatalogueOrdinal = UINT32_MAX;
    std::string           Category, Label, Description, RecipeTitle, RecipeCaption, ColourHex;
    std::vector<MaterialRecipeControl> RecipeControls;
    MaterialRecipeParameters Recipe;
};

// ⚠️ Single-thread ownership; containment/conflict checks do not lock a concurrently hostile filesystem.
class MaterialLibrary
{
  public:
    bool ConfigureDirectory(const std::filesystem::path &Directory);
    bool Refresh();
    void ConfigureFilter(std::string Search);
    bool OpenMaterial(const std::filesystem::path &RelativePath);
    void ReplaceSource(std::string Source);
    bool ApplySource();
    bool ConfigureStratumCoefficient(uint32_t Index, std::string_view Name, float Coefficient);
    bool ConfigurePaint(const AutomotivePaint &Paint);
    // 📝 Scales the authored Radiance spectrum so its peak component equals Strength (the Alloy
    // emissionStrength convention: emission colour is peak-normalized, strength is the peak).
    bool ConfigureEmissionStrength(float Strength);
    bool ConfigureStratumColour(uint32_t Index, const std::array<float, 3> &Spectrum, std::string_view Field = "Albedo");
    bool InsertStratum(uint32_t Index);
    bool RemoveStratum(uint32_t Index);
    bool ConfigureStratumChannel(uint32_t Index, std::string_view Channel, std::string_view Op);
    bool ForkProceduralChannel(uint32_t Index, std::string_view Channel);
    bool MergeProceduralChannel(uint32_t Index, std::string_view Channel, uint32_t SourceStratum, std::string_view SourceChannel);
    bool ConfigureProceduralParam(uint32_t Index, std::string_view Channel, std::string_view Key, double Value);
    bool ConfigureProceduralText(uint32_t Index, std::string_view Channel, std::string_view Key, std::string_view Text);
    bool RebakeProcedurals(uint32_t Extent = 512);
    [[nodiscard]] size_t QueryProceduralOverrideCount() const noexcept { return ProceduralOverrides.size(); }
    bool RevertSource();
    bool SaveMaterial();
    bool CreateMaterial(const std::filesystem::path &RelativePath, const std::string &Name);
    bool DuplicateMaterial(const std::filesystem::path &RelativePath);

    [[nodiscard]] const std::filesystem::path &QueryDirectory() const noexcept { return Directory; }
    [[nodiscard]] const std::filesystem::path &QuerySelection() const noexcept { return Selection; }
    [[nodiscard]] const std::vector<MaterialCatalogueEntry> &QueryEntries() const noexcept { return Entries; }
    [[nodiscard]] const std::vector<size_t> &QueryVisible() const noexcept { return Visible; }
    [[nodiscard]] const std::string &QuerySource() const noexcept { return Source; }
    [[nodiscard]] const std::string &QueryDiagnostic() const noexcept { return Diagnostic; }
    [[nodiscard]] const CompiledSurfaceMaterial *QueryApplied() const noexcept { return Applied.get(); }
    [[nodiscard]] bool QueryModified() const noexcept { return Source != SavedSource; }
    [[nodiscard]] bool QueryUnapplied() const noexcept { return Source != AppliedSource; }
    [[nodiscard]] bool QueryParked(const std::filesystem::path &RelativePath) const;
    [[nodiscard]] size_t QueryParkedCount() const noexcept { return ParkedSources.size(); }
    [[nodiscard]] bool QueryParkedEdits() const noexcept { return !ParkedSources.empty(); }
    [[nodiscard]] bool QueryTruncated() const noexcept { return Truncated; }
    [[nodiscard]] uint64_t QueryRevision() const noexcept { return Revision; }

  private:
    std::filesystem::path Directory, Selection;
    std::vector<MaterialCatalogueEntry> Entries;
    std::vector<size_t> Visible;
    std::string Source, SavedSource, AppliedSource, Search, Diagnostic;
    std::map<std::filesystem::path, std::string> ParkedSources;
    std::map<std::pair<size_t, std::string>, SurfaceTextureImage> ProceduralOverrides;
    std::unique_ptr<CompiledSurfaceMaterial> Applied;
    uint64_t Revision = 0;
    bool Truncated = false;

    std::filesystem::path ResolveMaterial(const std::filesystem::path &RelativePath) const;
    void FilterEntries();
    bool ConfigureStratumLiteral(uint32_t Index, std::string_view Name, const std::string &Numeric);
    bool PersistProceduralOverrides();
};
} // namespace Frontier
