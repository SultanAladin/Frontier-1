/*==========================================================================================================================================
                                                            TYPEFACEREGISTRY.CPP
==========================================================================================================================================*/
// 🧩 Register discovered typefaces and resolve available weights in the current ImGui atlas.

#include "TypefaceRegistry.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "ContentDirectory.h"
#include <cctype>
#include "imgui.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <cstring>

namespace Frontier
{

TypefaceRegistry *TypefaceRegistry::Current = nullptr;

namespace
{

// Tile preference order — Notch FontsTab.tsx FONTS[] first, then the two extras agreed for the port.
constexpr const char *PreferredFamilies[] = {"Inter",         "General Sans", "Archivo", "Space Grotesk",
                                             "Clash Display", "Montserrat",   "Poppins", "JetBrains Mono",
                                             "Lato",          "Fira Sans"};

// FontCodec renders "JetBrainsMono" as "Jet Brains Mono" (camel split); normalise both sides for matching.
std::string Squash(std::string_view S)
{
    std::string Out;
    Out.reserve(S.size());
    for (char C : S)
        if (C != ' ' && C != '-' && C != '_')
            Out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(C))));
    return Out;
}

constexpr FontWeightCategory Ladder[TypefaceRegistry::WeightCount] = {
    FontWeightCategory::Thin,    FontWeightCategory::ExtraLight, FontWeightCategory::Light,
    FontWeightCategory::Regular, FontWeightCategory::Medium,     FontWeightCategory::SemiBold,
    FontWeightCategory::Bold,    FontWeightCategory::ExtraBold,  FontWeightCategory::Black};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

TypefaceRegistry::TypefaceRegistry() noexcept : Families{}, Codec{}, Loaded(false)
{
}

void *TypefaceRegistry::LoadFace(const std::string &Path) noexcept
{
    ImGuiIO     &IO = ImGui::GetIO();
    ImFontConfig Config{};
    Config.OversampleH = 2; // font antialiasing "on" default; the Fonts tab switch flips RasterizerMultiply/oversample
    Config.OversampleV = 1;
    Config.PixelSnapH  = false;
    // Read through filesystem::path, not ImGui's narrow fopen path: Windows content paths may be Unicode.
    std::ifstream Input(DecodeContentPath(Path), std::ios::binary | std::ios::ate);
    if (!Input)
        return nullptr;
    const auto Length = Input.tellg();
    if (Length < 12 || Length > 32 * 1024 * 1024)
        return nullptr;
    std::vector<unsigned char> Bytes(static_cast<size_t>(Length));
    Input.seekg(0);
    if (!Input.read(reinterpret_cast<char *>(Bytes.data()), Length))
        return nullptr;
    const auto Read16 = [&](size_t Offset) { return (uint32_t(Bytes[Offset]) << 8) | Bytes[Offset + 1]; };
    const auto Read32 = [&](size_t Offset) { return (Read16(Offset) << 16) | Read16(Offset + 2); };
    if (Read32(0) != 0x00010000 && Read32(0) != 0x4F54544F)
        return nullptr;
    const auto TableCount = Read16(4);
    if (TableCount == 0 || 12u + TableCount * 16u > Bytes.size())
        return nullptr;
    for (uint32_t TableIndex = 0; TableIndex < TableCount; ++TableIndex)
    {
        const auto Offset = Read32(12 + TableIndex * 16 + 8);
        const auto Extent = Read32(12 + TableIndex * 16 + 12);
        if (Offset > Bytes.size() || Extent > Bytes.size() - Offset)
            return nullptr;
    }
    void *OwnedBytes = IM_ALLOC(Bytes.size());
    if (!OwnedBytes)
        return nullptr;
    std::memcpy(OwnedBytes, Bytes.data(), Bytes.size());
    ImFont *Font = IO.Fonts->AddFontFromMemoryTTF(OwnedBytes, static_cast<int>(Bytes.size()), 16.0f, &Config);
    return Font;
}

uint32_t TypefaceRegistry::Load(const std::filesystem::path &ArchiveRoot) noexcept
{
    DiagnosticJournal::Append("Fonts", "Archive=" + DiagnosticJournal::EncodePath(ResolveContentDirectory(ArchiveRoot)));
    if (Loaded)
        return QueryFamilyCount();
    if (ImGui::GetCurrentContext() == nullptr)
        return 0u;

    if (!Codec.ScanDirectory(EncodeContentPath(ResolveContentDirectory(ArchiveRoot))))
        return 0u;
    std::vector<const FontFamilyIndex *> Ordered;
    for (const char *Wanted : PreferredFamilies)
    {
        for (const auto &Family : Codec.QueryDiscoveredFamilies())
            if (Squash(Family.FamilyName) == Squash(Wanted))
                Ordered.push_back(&Family);
    }
    for (const auto &Family : Codec.QueryDiscoveredFamilies())
        if (std::find(Ordered.begin(), Ordered.end(), &Family) == Ordered.end())
            Ordered.push_back(&Family);

    for (const auto *Index : Ordered)
    {
        const std::string Wanted = Index->FamilyName;
        TypefaceFamily    Family;
        Family.Name = Wanted; // canonical spelling (FontCodec's camel-split may differ)
        for (const char *Preferred : PreferredFamilies)
            if (Squash(Preferred) == Squash(Wanted))
                Family.Name = Preferred;
        Family.Description = Index->Description;
        Family.Monospaced  = Squash(Wanted).find("mono") != std::string::npos;

        for (FontWeightCategory W : Ladder)
        {
            const FontVariantIndex *V = nullptr;
            for (const FontVariantIndex &Var : Index->Variants)
                if (Var.Weight == W && Var.Style == FontStyleCategory::Normal)
                {
                    V = &Var;
                    break;
                }
            if (!V)
                continue;
            void *Handle = LoadFace(V->FilePath);
            if (!Handle)
            {
                ++RejectedFaces;
                DiagnosticJournal::Append("FontRejected", V->FilePath);
                continue;
            }
            Family.Faces.push_back(TypefaceFace{W, Handle});
        }

        // Accept even single-weight families, including newly supplied local fonts.
        if (Family.Faces.empty())
            continue;

        Families.push_back(std::move(Family));
    }
    Loaded = !Families.empty();
    DiagnosticJournal::Append("Fonts", "families=" + std::to_string(QueryFamilyCount()) + " rejected=" + std::to_string(RejectedFaces));
    return QueryFamilyCount();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        QUERIES
//------------------------------------------------------------------------------------------------------------------------

int32_t TypefaceRegistry::ResolveFamilyIndex(std::string_view Name) const noexcept
{
    const std::string Key = Squash(Name);
    for (size_t I = 0; I < Families.size(); ++I)
        if (Squash(Families[I].Name) == Key)
            return static_cast<int32_t>(I);
    return -1;
}

bool TypefaceRegistry::ContainsWeight(uint32_t FamilyIndex, FontWeightCategory Weight) const noexcept
{
    const TypefaceFamily *F = QueryFamily(FamilyIndex);
    if (!F)
        return false;
    return std::any_of(F->Faces.begin(), F->Faces.end(),
                       [&](const TypefaceFace &Face) { return Face.Weight == Weight; });
}

const TypefaceFace *TypefaceRegistry::Resolve(uint32_t FamilyIndex, FontWeightCategory Wanted) const noexcept
{
    const TypefaceFamily *F = QueryFamily(FamilyIndex);
    if (!F || F->Faces.empty())
        return nullptr;
    const int32_t       Want         = static_cast<int32_t>(Wanted);
    const TypefaceFace *Best         = nullptr;
    int32_t             BestDistance = 1 << 30;
    for (const TypefaceFace &Face : F->Faces)
    {
        const int32_t Have     = static_cast<int32_t>(Face.Weight);
        const int32_t Distance = std::abs(Have - Want);
        // CSS-style tie-break: at or above 400 prefer heavier, below 400 prefer lighter.
        const bool PreferThis =
            Distance < BestDistance || (Distance == BestDistance && Best &&
                                        ((Want >= 400 && Have > static_cast<int32_t>(Best->Weight)) ||
                                         (Want < 400 && Have < static_cast<int32_t>(Best->Weight))));
        if (PreferThis)
        {
            Best         = &Face;
            BestDistance = Distance;
        }
    }
    return Best;
}

void *TypefaceRegistry::QueryHandle(uint32_t FamilyIndex, FontWeightCategory Wanted) const noexcept
{
    const TypefaceFace *Face = Resolve(FamilyIndex, Wanted);
    return Face ? Face->Handle : nullptr;
}

const char *TypefaceRegistry::WeightLabel(FontWeightCategory Weight) noexcept
{
    switch (Weight)
    {
    case FontWeightCategory::Thin:
        return "Thin";
    case FontWeightCategory::ExtraLight:
        return "ExtraLight";
    case FontWeightCategory::Light:
        return "Light";
    case FontWeightCategory::Regular:
        return "Regular";
    case FontWeightCategory::Medium:
        return "Medium";
    case FontWeightCategory::SemiBold:
        return "SemiBold";
    case FontWeightCategory::Bold:
        return "Bold";
    case FontWeightCategory::ExtraBold:
        return "ExtraBold";
    case FontWeightCategory::Black:
        return "Black";
    }
    return "Regular";
}

FontWeightCategory TypefaceRegistry::WeightAt(uint32_t Ordinal) noexcept
{
    return Ladder[std::min(Ordinal, WeightCount - 1u)];
}

uint32_t TypefaceRegistry::WeightOrdinal(FontWeightCategory Weight) noexcept
{
    for (uint32_t I = 0u; I < WeightCount; ++I)
        if (Ladder[I] == Weight)
            return I;
    return 3u;
}

} // namespace Frontier
