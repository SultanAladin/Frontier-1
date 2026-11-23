/*==========================================================================================================================================
                                                             TYPEFACEREGISTRY.H
==========================================================================================================================================*/
// 🧩 Register discovered typefaces and resolve available weights in the current ImGui atlas.

#pragma once

#include "FontCodec.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     TYPEFACE FACE
//------------------------------------------------------------------------------------------------------------------------

struct TypefaceFace
{
    FontWeightCategory Weight = FontWeightCategory::Regular;
    void              *Handle = nullptr; // [-] backend face (ImFont*), opaque above this seam
};

struct TypefaceFamily
{
    std::string               Name;        // [name] "Inter", "General Sans" …
    std::string               Description; // [text] from the archive TOML ("Author", "Clean & Modern" …)
    bool                      Monospaced = false;
    std::vector<TypefaceFace> Faces; // [faces] ascending weight, one per static file
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPEFACE REGISTRY
//------------------------------------------------------------------------------------------------------------------------

class TypefaceRegistry
{
  public:
    static constexpr uint32_t WeightCount = 9u; // Thin … Black (100 … 900)

    TypefaceRegistry() noexcept;

    // Scan + load. Must run after the display backend context exists and before the first frame. Returns the number
    //    of families accepted into the strip. Safe to call once; later calls are ignored.
    uint32_t           Load(const std::filesystem::path &ArchiveRoot = "EngineContent/FontArchives") noexcept;
    [[nodiscard]] bool QueryLoaded() const noexcept { return Loaded; }

    [[nodiscard]] uint32_t QueryRejectedFaceCount() const noexcept { return RejectedFaces; }

    // Families in tile order.
    [[nodiscard]] uint32_t QueryFamilyCount() const noexcept { return static_cast<uint32_t>(Families.size()); }
    [[nodiscard]] const TypefaceFamily *QueryFamily(uint32_t Index) const noexcept
    {
        return Index < Families.size() ? &Families[Index] : nullptr;
    }
    [[nodiscard]] int32_t ResolveFamilyIndex(std::string_view Name) const noexcept;

    // Faces. Resolve() snaps to the nearest weight that exists (ties → the heavier face, matching CSS font matching
    //    for weights ≥ 400 and the lighter face below 400).
    [[nodiscard]] const TypefaceFace *Resolve(uint32_t FamilyIndex, FontWeightCategory Wanted) const noexcept;
    [[nodiscard]] bool                ContainsWeight(uint32_t FamilyIndex, FontWeightCategory Weight) const noexcept;
    [[nodiscard]] void               *QueryHandle(uint32_t FamilyIndex, FontWeightCategory Wanted) const noexcept;

    // Weight naming (Notch FontsTab chip labels).
    [[nodiscard]] static const char        *WeightLabel(FontWeightCategory Weight) noexcept;
    [[nodiscard]] static FontWeightCategory WeightAt(uint32_t Ordinal) noexcept; // 0 → Thin … 8 → Black
    [[nodiscard]] static uint32_t           WeightOrdinal(FontWeightCategory Weight) noexcept;

    // Process-wide instance the PixelSpace text path reads from (nullptr until a host installs one).
    static void                            Install(TypefaceRegistry *Registry) noexcept { Current = Registry; }
    [[nodiscard]] static TypefaceRegistry *QueryCurrent() noexcept { return Current; }

  private:
    void *LoadFace(const std::string &Path) noexcept;

    std::vector<TypefaceFamily> Families;
    FontCodec                   Codec;
    bool                        Loaded;
    uint32_t                    RejectedFaces = 0;

    static TypefaceRegistry *Current;
};

} // namespace Frontier
