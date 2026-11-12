/*============================================================================================================================================
                                                            TYPOGRAPHYREPOSITORY.H
============================================================================================================================================*/
// 🧩 Discover and load .FontSpace typeface weights into Dear ImGui with CSS weight fallback resolution

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct ImFont;

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct TypefaceWeightEntry
{
    FontWeightClass Weight = FontWeightClass::Regular; // [-] - CSS numeric weight class [100..900]
    ImFont*         Font   = nullptr;                  // [-] - Rasterized Dear ImGui font face
};

struct TypefaceGroup
{
    std::string                      Name       {};    // [-] - Display name of the typeface group
    bool                             Monospaced = false; // [-] - Fixed-advance monospace layout
    std::vector<TypefaceWeightEntry> Faces      {};    // [-] - Ordered weights available in this group
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class TypographyRepository
{
public:
    static constexpr uint32_t WeightCount = 9u; // [count] - Standard CSS weight classes (100..900)

    TypographyRepository() noexcept = default;

    uint32_t LoadRepository(const std::filesystem::path& RepositoryRoot) noexcept;

    [[nodiscard]] uint32_t                   QueryGroupCount() const noexcept;
    [[nodiscard]] const TypefaceGroup*       QueryGroup(uint32_t GroupIndex) const noexcept;
    [[nodiscard]] int32_t                    ResolveGroupIndex(std::string_view Name) const noexcept;
    [[nodiscard]] bool                       ContainsWeight(uint32_t GroupIndex, FontWeightClass Weight) const noexcept;
    [[nodiscard]] const TypefaceWeightEntry* ResolveWeight(uint32_t GroupIndex, FontWeightClass Wanted) const noexcept;
    [[nodiscard]] ImFont*                    QueryFont(uint32_t GroupIndex, FontWeightClass Wanted) const noexcept;
    [[nodiscard]] ImFont*                    QueryTabStripFont() const noexcept;

    static void                              AssignCurrent(TypographyRepository* Repository) noexcept;
    [[nodiscard]] static TypographyRepository*    QueryCurrent() noexcept;

    [[nodiscard]] static const char*         WeightLabel(FontWeightClass Weight) noexcept;
    [[nodiscard]] static FontWeightClass     WeightAtOrdinal(uint32_t Ordinal) noexcept;
    [[nodiscard]] static uint32_t            WeightOrdinal(FontWeightClass Weight) noexcept;

private:
    static ImFont* LoadFontFile(const std::filesystem::path& FontPath, float PixelHeight = 24.0f) noexcept;

    std::vector<TypefaceGroup> Groups       {};      // [-] - Loaded typeface groups ordered with Inter first
    ImFont*                    TabStripFont = nullptr;// [-] - Dedicated 13px face for trapezoidal tabs & close glyphs
    bool                       Loaded       = false; // [-] - Repository load completion guard

    static TypographyRepository* ActiveRepository;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline uint32_t TypographyRepository::QueryGroupCount() const noexcept
{
    return static_cast<uint32_t>(Groups.size());
}

inline ImFont* TypographyRepository::QueryTabStripFont() const noexcept
{
    return TabStripFont;
}

inline void TypographyRepository::AssignCurrent(TypographyRepository* Repository) noexcept
{
    ActiveRepository = Repository;
}

inline TypographyRepository* TypographyRepository::QueryCurrent() noexcept
{
    return ActiveRepository;
}

} // namespace Frontier
