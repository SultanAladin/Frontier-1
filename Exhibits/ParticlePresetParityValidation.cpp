/*============================================================================================================================================
                                                    PARTICLEPRESETPARITYVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase 6 102-preset & 15-category parity validation rendering one hero preset per category for HTML-vs-C++ visual comparison

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/ParticleExtension.h"
#include "Engine/ParticleSpace/Presets/ParticlePresetCatalogue.h"
#include "Engine/ParticleSpace/Simulation/ParticleMetricsCollector.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ExternalPackages/stb/stb_image_write.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

using namespace Frontier::ParticleSpace;

namespace
{

struct CategoryHeroEntry
{
    const char* CategoryName;
    const char* PresetId;
};

// All 15 canonical VFX categories (14 active + Quarantine) represented by their flagship preset
constexpr CategoryHeroEntry kCategoryHeroes[15] = {
    { "Ballistics & Sparks", "welding-grinder-sparks" },
    { "Pyro & Water",        "fire"                   },
    { "Storms & Weather",    "rain"                   },
    { "Wind-Blown",          "boids-flock"            },
    { "Atmosphere & Sky",    "fireworks-spectacle"    },
    { "Silk & Curl",         "bokeh-object-pulse"     },
    { "Shields & Tech",      "energy-shield-hex"      },
    { "Transitions",         "derez-cube"             },
    { "Space & Distortion",  "black-hole"             },
    { "Cloth & Rigging",     "flag-banner"            },
    { "Foliage & Flora",     "interactive-tree"       },
    { "Motion & Grids",      "effector-grid-cubes"    },
    { "Ferrofluid & Fields", "ferrofluid-spikes"      },
    { "Fracture & Cracks",   "ground-crack-quake"     },
    { "Quarantine",          "car-puddle-splash"      }
};

void SplatAdditive(std::vector<uint8_t>& Img, int W, int H, int X0, int Y0, int CW, int CH,
                   float Px, float Py, float Radius, float R, float G, float B, float A)
{
    const int Cx = X0 + static_cast<int>(Px);
    const int Cy = Y0 + static_cast<int>(Py);
    const int Rad = std::clamp(static_cast<int>(std::ceil(Radius)), 1, 9);
    for (int Dy = -Rad; Dy <= Rad; ++Dy)
    {
        const int Y = Cy + Dy;
        if (Y < Y0 || Y >= Y0 + CH || Y < 0 || Y >= H) continue;
        for (int Dx = -Rad; Dx <= Rad; ++Dx)
        {
            const int X = Cx + Dx;
            if (X < X0 || X >= X0 + CW || X < 0 || X >= W) continue;
            const float Dist = std::sqrt(static_cast<float>(Dx * Dx + Dy * Dy)) / static_cast<float>(Rad);
            if (Dist > 1.0f) continue;
            const float Wgt = std::pow(1.0f - Dist, 1.8f) * A;
            const std::size_t Idx = static_cast<std::size_t>((Y * W + X) * 4);
            Img[Idx + 0] = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(Img[Idx + 0]) + R * 255.0f * Wgt));
            Img[Idx + 1] = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(Img[Idx + 1]) + G * 255.0f * Wgt));
            Img[Idx + 2] = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(Img[Idx + 2]) + B * 255.0f * Wgt));
            Img[Idx + 3] = 255u;
        }
    }
}

void DrawTileBackdrop(std::vector<uint8_t>& Img, int W, int X0, int Y0, int CW, int CH)
{
    for (int Y = 0; Y < CH; ++Y)
    {
        const float V = static_cast<float>(Y) / static_cast<float>(CH);
        for (int X = 0; X < CW; ++X)
        {
            const bool Border = (X == 0 || Y == 0 || X == CW - 1 || Y == CH - 1);
            const std::size_t Idx = static_cast<std::size_t>(((Y0 + Y) * W + (X0 + X)) * 4);
            Img[Idx + 0] = Border ? 48u : static_cast<uint8_t>(11.0f + V * 12.0f);
            Img[Idx + 1] = Border ? 58u : static_cast<uint8_t>(14.0f + V * 16.0f);
            Img[Idx + 2] = Border ? 74u : static_cast<uint8_t>(22.0f + V * 24.0f);
            Img[Idx + 3] = 255u;
        }
    }
}

} // namespace

int main()
{
    std::cout << "=== ParticleSpace Phase 6 102-Preset & 15-Category Parity Validation ===\n";

    // 1. Audit all 102 presets for instantiation, 16-frame stability, finite bounds, and non-zero live count
    const auto AllPresets = QueryAllParticlePresets();
    if (AllPresets.size() != 102u)
    {
        std::cerr << "[FAIL] Expected 102 presets, got " << AllPresets.size() << "\n";
        return 1;
    }

    uint32_t CleanPresets = 0u;
    for (const auto& P : AllPresets)
    {
        ParticleDocument Doc = InstantiateDocumentFromPresetId(P.Identifier);
        Doc.Rigs[0].SlotCapacity = std::min<uint32_t>(Doc.Rigs[0].SlotCapacity, 512u);
        ParticleExtension Ext {};
        if (!Ext.Initialize(Doc)) continue;
        Ext.TriggerBurst(0u, 128u);
        for (uint32_t F = 0u; F < 16u; ++F)
        {
            Ext.Tick(1.0 / 60.0);
        }
        const auto M = ParticleMetricsCollector::CollectFromSlots(Ext.InspectParticleSlots());
        if (M.NaNOrInfCount == 0u && M.LiveCount > 0u) ++CleanPresets;
        else std::cerr << "[FAIL] Preset '" << P.Identifier << "' live=" << M.LiveCount << " nan=" << M.NaNOrInfCount << "\n";
    }
    std::cout << "[PASS] 102-preset full sweep: " << CleanPresets << "/102 presets live & finite across 16 steps\n";
    if (CleanPresets != 102u) return 1;

    // 2. Render 5x3 Contact Sheet covering ALL 15 VFX categories + individual hero tiles for side-by-side HTML parity!
    const int Cols = 5, Rows = 3;
    const int CW = 320, CH = 240;
    const int SheetW = Cols * CW;
    const int SheetH = Rows * CH;
    std::vector<uint8_t> Sheet(static_cast<std::size_t>(SheetW * SheetH * 4), 255u);

    const std::filesystem::path ParityDir = "Exhibits/Images/Particles/Parity";
    std::filesystem::create_directories(ParityDir);

    for (int I = 0; I < 15; ++I)
    {
        const auto& Hero = kCategoryHeroes[I];
        ParticleDocument Doc = InstantiateDocumentFromPresetId(Hero.PresetId);
        Doc.Collider.BallActive     = true;
        Doc.Collider.BallCenterM[0] = 0.7f;
        Doc.Collider.BallCenterM[1] = 1.5f;
        Doc.Collider.BallCenterM[2] = 0.5f;
        Doc.Rigs[0].SlotCapacity    = std::min<uint32_t>(Doc.Rigs[0].SlotCapacity, 1600u);

        ParticleExtension Ext {};
        (void)Ext.Initialize(Doc);
        Ext.TriggerBurst(0u, 640u);
        for (uint32_t F = 0u; F < 72u; ++F)
        {
            Ext.Tick(1.0 / 60.0);
        }
        const auto Slots = Ext.InspectParticleSlots();

        const int X0 = (I % Cols) * CW;
        const int Y0 = (I / Cols) * CH;
        DrawTileBackdrop(Sheet, SheetW, X0, Y0, CW, CH);

        std::vector<uint8_t> SingleTile(static_cast<std::size_t>(CW * CH * 4), 255u);
        DrawTileBackdrop(SingleTile, CW, 0, 0, CW, CH);

        const float Yaw = 0.52f, Pitch = 0.34f;
        const float Cy = std::cos(Yaw), Sy = std::sin(Yaw);
        const float Cp = std::cos(Pitch), Sp = std::sin(Pitch);

        // Auto-frame every category tile around its live 3D particle centroid & radius
        float MeanX = 0.0f, MeanY = 0.0f, MeanZ = 0.0f;
        uint32_t AliveN = 0u;
        for (const auto& S : Slots)
        {
            if (!IsParticleSlotAlive(S)) continue;
            MeanX += S.PositionMeters[0];
            MeanY += S.PositionMeters[1];
            MeanZ += S.PositionMeters[2];
            ++AliveN;
        }
        if (AliveN > 0u)
        {
            MeanX /= static_cast<float>(AliveN);
            MeanY /= static_cast<float>(AliveN);
            MeanZ /= static_cast<float>(AliveN);
        }
        float MaxR = 0.65f;
        for (const auto& S : Slots)
        {
            if (!IsParticleSlotAlive(S)) continue;
            const float Dx = S.PositionMeters[0] - MeanX;
            const float Dy = S.PositionMeters[1] - MeanY;
            const float Dz = S.PositionMeters[2] - MeanZ;
            MaxR = std::max(MaxR, std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
        }
        const float CamDist = std::max(2.5f, MaxR * 2.35f);

        for (const auto& S : Slots)
        {
            if (!IsParticleSlotAlive(S)) continue;
            const float X = S.PositionMeters[0] - MeanX;
            const float Y = S.PositionMeters[1] - MeanY;
            const float Z = S.PositionMeters[2] - MeanZ;
            const float Rx = X * Cy - Z * Sy;
            const float Rz = X * Sy + Z * Cy;
            const float Ry = Y * Cp - Rz * Sp;
            const float Depth = Rz * Cp + Y * Sp + CamDist;
            if (Depth < 0.35f) continue;

            const float Proj = (static_cast<float>(CH) * 0.88f) / Depth;
            const float Px = static_cast<float>(CW) * 0.5f + Rx * Proj;
            const float Py = static_cast<float>(CH) * 0.53f - Ry * Proj;
            const float Rad = std::clamp(S.SizeMeters * Proj * 0.55f, 1.8f, 6.2f);
            const float Alpha = std::clamp(S.ColourLinear[3], 0.45f, 0.95f);
            const float R = std::max(S.ColourLinear[0], 0.14f);
            const float G = std::max(S.ColourLinear[1], 0.16f);
            const float B = std::max(S.ColourLinear[2], 0.22f);

            SplatAdditive(Sheet, SheetW, SheetH, X0, Y0, CW, CH, Px, Py, Rad, R, G, B, Alpha);
            SplatAdditive(SingleTile, CW, CH, 0, 0, CW, CH, Px, Py, Rad, R, G, B, Alpha);
        }

        const std::filesystem::path TilePath = ParityDir / (std::string("Cpp_") + Hero.PresetId + ".png");
        stbi_write_png(TilePath.string().c_str(), CW, CH, 4, SingleTile.data(), CW * 4);
    }

    const std::filesystem::path SheetPath = ParityDir / "Cpp_All15Categories_ContactSheet.png";
    if (!stbi_write_png(SheetPath.string().c_str(), SheetW, SheetH, 4, Sheet.data(), SheetW * 4))
    {
        std::cerr << "[FAIL] Failed to write Cpp_All15Categories_ContactSheet.png\n";
        return 1;
    }

    std::cout << "[PASS] Rendered all 15 VFX category heroes + " << SheetPath.string() << "\n";
    return 0;
}
