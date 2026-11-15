/*============================================================================================================================================
                                                    PARTICLEMOTIONGRIDVALIDATION.CPP
============================================================================================================================================*/
// 🧩 High-resolution C++ validation & visual proof sheet for Motion & Grids (Kinetic Hex Torus Matrix + Obsidian Halftone & Tiles + Placeable Effectors on Controlled Integrator Vehicle)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/EffectorGridSolver.h"
#include "Engine/ParticleSpace/Origins/ControlVehicleSurfaceLoader.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/ParticleSimulationCoordinator.h"
#include "Engine/ParticleSpace/Presets/ParticlePresetRepository.h"
#include "Engine/ParticleSpace/Simulation/ParticleStrandRenderer.h"

#include <algorithm>
#include <array>
#include <cctype>
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

int gFailures = 0;

void ExpectCheck(bool Condition, const char* Message)
{
    if (!Condition)
    {
        std::cerr << "[FAIL] " << Message << "\n";
        ++gFailures;
    }
    else
    {
        std::cout << "[PASS] " << Message << "\n";
    }
}

void BlendBannerText(
    std::vector<uint8_t>& Rgba,
    int                   ImgW,
    int                   ImgH,
    int                   X0,
    int                   Y0,
    const char*           Text,
    uint8_t               R,
    uint8_t               G,
    uint8_t               B)
{
    // Minimal crisp 5x7 uppercase bitmap font for proof-sheet tile captions
    static const std::array<uint8_t, 7> kBlank = { 0, 0, 0, 0, 0, 0, 0 };
    auto GlyphBits = [&](char Ch) -> std::array<uint8_t, 7> {
        switch (std::toupper(static_cast<unsigned char>(Ch)))
        {
            case 'A': return { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
            case 'B': return { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
            case 'C': return { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
            case 'D': return { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C };
            case 'E': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
            case 'F': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
            case 'G': return { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F };
            case 'H': return { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
            case 'I': return { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E };
            case 'J': return { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C };
            case 'K': return { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
            case 'L': return { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
            case 'M': return { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
            case 'N': return { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
            case 'O': return { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
            case 'P': return { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
            case 'Q': return { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
            case 'R': return { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
            case 'S': return { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
            case 'T': return { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
            case 'U': return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
            case 'V': return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
            case 'W': return { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 };
            case 'X': return { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
            case 'Y': return { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
            case 'Z': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
            case '0': return { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
            case '1': return { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
            case '2': return { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F };
            case '3': return { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E };
            case '4': return { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
            case '5': return { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E };
            case '6': return { 0x0E, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x0E };
            case '7': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
            case '8': return { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
            case '9': return { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E };
            case '+': return { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 };
            case '-': return { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
            case '&': return { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D };
            case '.': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C };
            case ':': return { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 };
            case '/': return { 0x01, 0x02, 0x04, 0x04, 0x08, 0x10, 0x00 };
            case '(': return { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 };
            case ')': return { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 };
            default:  return kBlank;
        }
    };

    int CursorX = X0;
    for (const char* P = Text; *P != '\0'; ++P)
    {
        const auto Rows = GlyphBits(*P);
        for (int Ry = 0; Ry < 7; ++Ry)
        {
            for (int Rx = 0; Rx < 5; ++Rx)
            {
                if (((Rows[static_cast<std::size_t>(Ry)] >> (4 - Rx)) & 1u) == 0u) continue;
                for (int Sy = 0; Sy < 2; ++Sy)
                {
                    for (int Sx = 0; Sx < 2; ++Sx)
                    {
                        const int Px = CursorX + Rx * 2 + Sx;
                        const int Py = Y0 + Ry * 2 + Sy;
                        if (Px < 0 || Py < 0 || Px >= ImgW || Py >= ImgH) continue;
                        const std::size_t Off = static_cast<std::size_t>((Py * ImgW + Px) * 4);
                        Rgba[Off + 0u] = R;
                        Rgba[Off + 1u] = G;
                        Rgba[Off + 2u] = B;
                        Rgba[Off + 3u] = 255u;
                    }
                }
            }
        }
        CursorX += 12;
    }
}

void FillRectRgba8(
    std::vector<uint8_t>& Rgba,
    int                   ImgW,
    int                   ImgH,
    int                   X0,
    int                   Y0,
    int                   W,
    int                   H,
    uint8_t               R,
    uint8_t               G,
    uint8_t               B)
{
    for (int Y = std::max(0, Y0); Y < std::min(ImgH, Y0 + H); ++Y)
    {
        for (int X = std::max(0, X0); X < std::min(ImgW, X0 + W); ++X)
        {
            const std::size_t Off = static_cast<std::size_t>((Y * ImgW + X) * 4);
            Rgba[Off + 0u] = R;
            Rgba[Off + 1u] = G;
            Rgba[Off + 2u] = B;
            Rgba[Off + 3u] = 255u;
        }
    }
}

struct RenderedPanel
{
    std::string          Caption;
    std::vector<uint8_t> Rgba;
    int                  Width  = 0;
    int                  Height = 0;
};

void VerifySlotsHealthy(std::span<const ParticleSlot> Slots, const char* Label)
{
    uint32_t Alive = 0u;
    uint32_t NanCount = 0u;
    float MinW = 10.0f, MaxW = -10.0f;
    for (const auto& S : Slots)
    {
        if (!QueryParticleSlotAlive(S)) continue;
        ++Alive;
        for (int K = 0; K < 3; ++K)
        {
            if (!std::isfinite(S.PositionMeters[K]) || !std::isfinite(S.VelocityMps[K])) ++NanCount;
        }
        if (!std::isfinite(S.SizeMeters) || !std::isfinite(S.AuxScalar)) ++NanCount;
        MinW = std::min(MinW, S.AuxScalar);
        MaxW = std::max(MaxW, S.AuxScalar);
    }
    std::cout << "  [" << Label << "] alive=" << Alive << " weight=[" << MinW << ", " << MaxW << "] NaNs=" << NanCount << "\n";
    ExpectCheck(Alive > 0u && NanCount == 0u && (MaxW - MinW) > 0.15f, Label);
}

} // namespace

int main()
{
    std::cout << "=== Frontier ParticleMotionGridValidation (Controlled Integrator + Motion & Grids) ===\n";

    // 1. Validate real Controlled Integrator (ControlVehicle) FCT1 geometry & anchor sampler
    const auto& Vehicle = QueryControlVehicleGeometrySet();
    std::cout << "  ControlVehicle triangles=" << Vehicle.Triangles.size()
              << " bodyTris=" << Vehicle.BodyTriCount
              << " boundsY=[" << Vehicle.BoundsMin[1] << ", " << Vehicle.BoundsMax[1] << "]"
              << " lengthX=" << (Vehicle.BoundsMax[0] - Vehicle.BoundsMin[0]) << "m\n";
    ExpectCheck(Vehicle.LoadedFromDisk && Vehicle.Triangles.size() >= 20000u && Vehicle.BodyTriCount >= 2500u,
                "Controlled Integrator (ControlVehicle) FCT1 Body + 4 Rims + 4 Tyres loaded from disk");
    ExpectCheck(std::fabs(Vehicle.BoundsMin[1]) < 0.01f && Vehicle.BoundsMax[1] > 1.2f,
                "Controlled Integrator rests cleanly on Studio Floor Y = 0");

    const auto Anchors = SampleControlVehicleAnchors(2200u, 1337u, true, false);
    ExpectCheck(Anchors.size() == 2200u, "SampleControlVehicleAnchors produces 2200 blue-noise surface anchors");

    const std::filesystem::path OutDir("Exhibits/Images/Particles/Parity");
    std::filesystem::create_directories(OutDir);

    constexpr int RenderW = 720;
    constexpr int RenderH = 540;
    std::vector<RenderedPanel> Panels;

    auto ExecuteFrameCapture = [&](const char* PresetId,
                             const char* Slug,
                             const char* Caption,
                             const StrandCameraSpec& Cam,
                             float SimSeconds,
                             auto&& MutateDoc)
    {
        ParticleDocument Doc = InstantiateDocumentFromPresetId(PresetId);
        MutateDoc(Doc);

        ParticleSimulationCoordinator Ext {};
        (void)Ext.Initialize(Doc);

        constexpr float Dt = 1.0f / 60.0f;
        const int Steps = std::max(1, static_cast<int>(std::round(SimSeconds / Dt)));
        for (int S = 0; S < Steps; ++S)
        {
            Ext.Tick(Dt);
        }

        const auto Slots = Ext.InspectParticleSlots();
        VerifySlotsHealthy(Slots, Caption);

        std::vector<uint8_t> Rgba;
        RenderDynamicStrandRigRgba8(
            Ext.InspectDocument().Rigs[0],
            Slots,
            Ext.InspectStrandVertices(),
            Cam,
            RenderW,
            RenderH,
            Rgba,
            SimSeconds);

        const auto SinglePath = OutDir / (std::string("MotionGrid_") + Slug + ".png");
        stbi_write_png(SinglePath.string().c_str(), RenderW, RenderH, 4, Rgba.data(), RenderW * 4);
        Panels.push_back({ Caption, std::move(Rgba), RenderW, RenderH });
    };

    // Panel 1: Obsidian Control Vehicle · Halftone & Tiles (Front 3/4 Hero View)
    {
        StrandCameraSpec Cam {};
        Cam.FocusMeters[0] = 0.05f; Cam.FocusMeters[1] = 0.78f; Cam.FocusMeters[2] = 0.0f;
        Cam.YawRadians     = 0.78f;
        Cam.PitchRadians   = 0.28f;
        Cam.DistanceMeters = 7.45f;
        Cam.FocalMm        = 34.0f;
        ExecuteFrameCapture(
            "effector-hand-halftone",
            "01_ControlVehicle_ObsidianHalftoneTiles_Front",
            "01 OBSIDIAN CONTROL VEHICLE : HALFTONE & TILES (FRONT 3/4)",
            Cam, 2.25f,
            [](ParticleDocument&) {});
    }

    // Panel 2: Obsidian Control Vehicle · Halftone & Tiles (Rear 3/4 Elevation View)
    {
        StrandCameraSpec Cam {};
        Cam.FocusMeters[0] = -0.05f; Cam.FocusMeters[1] = 0.82f; Cam.FocusMeters[2] = 0.0f;
        Cam.YawRadians     = -2.35f;
        Cam.PitchRadians   = 0.34f;
        Cam.DistanceMeters = 7.60f;
        Cam.FocalMm        = 34.0f;
        ExecuteFrameCapture(
            "effector-hand-halftone",
            "02_ControlVehicle_ObsidianHalftoneTiles_Rear",
            "02 OBSIDIAN CONTROL VEHICLE : HALFTONE & TILES (REAR 3/4)",
            Cam, 2.85f,
            [](ParticleDocument&) {});
    }

    // Panel 3: Kinetic Hex Matrix · Control Vehicle (Gold & Cyan 3D Beveled Hex Plates on Controlled Integrator!)
    {
        StrandCameraSpec Cam {};
        Cam.FocusMeters[0] = 0.05f; Cam.FocusMeters[1] = 0.78f; Cam.FocusMeters[2] = 0.0f;
        Cam.YawRadians     = 0.82f;
        Cam.PitchRadians   = 0.30f;
        Cam.DistanceMeters = 7.45f;
        Cam.FocalMm        = 34.0f;
        ExecuteFrameCapture(
            "effector-torus-hex",
            "03_ControlVehicle_KineticHexMatrix",
            "03 KINETIC HEX MATRIX : CONTROL VEHICLE (GOLD/CYAN)",
            Cam, 1.80f,
            [](ParticleDocument&) {});
    }

    // Panel 4: Kinetic Hex Torus Matrix (Original Revolving Torus 1:1 Parity!)
    {
        StrandCameraSpec Cam {};
        Cam.FocusMeters[0] = 0.0f; Cam.FocusMeters[1] = 1.65f; Cam.FocusMeters[2] = 0.0f;
        Cam.YawRadians     = 0.56f;
        Cam.PitchRadians   = 0.44f;
        Cam.DistanceMeters = 6.20f;
        Cam.FocalMm        = 32.0f;
        ExecuteFrameCapture(
            "effector-torus-hex",
            "04_KineticHexTorusMatrix",
            "04 KINETIC HEX TORUS MATRIX (REVOLVING TORUS)",
            Cam, 1.75f,
            [](ParticleDocument& Doc) {
                auto& Rig = Doc.Rigs[0];
                Rig.SlotCapacity = 760u;
                auto& Eff = *Rig.Dynamics.Effector;
                Eff.HostSurface         = EffectorHostSurfaceCategory::TorusRing;
                Eff.PrimitiveShape      = EffectorPrimitiveShape::HexagonPlate;
                Eff.Driver              = EffectorDriverCategory::ExpandingPulseRing;
                Eff.GridColumns         = 38u;
                Eff.GridRows            = 20u;
                Eff.OriginExtentM[0]    = 1.45f;
                Eff.OriginExtentM[1]    = 1.45f;
                Eff.OriginExtentM[2]    = 1.45f;
                Eff.WaveFrequencyPerM   = 1.25f;
                Eff.WaveDriveSpeed      = 0.72f;
                Eff.MinScaleFraction    = 0.22f;
                Eff.MaxExtrusionMeters  = 0.22f;
                Eff.FalloffContrast     = 1.45f;
                Eff.BaseCellWidthMeters = 0.135f;
                SurfaceAnchorOrigin S {};
                S.Preset = SurfacePresetCategory::TorusRing;
                S.CenterMeters[0] = 0.0f; S.CenterMeters[1] = 1.65f; S.CenterMeters[2] = 0.0f;
                S.ScaleMeters[0]  = 1.45f; S.ScaleMeters[1] = 1.45f; S.ScaleMeters[2]  = 1.45f;
                Rig.Origin = S;
            });
    }

    // Panel 5: Control Vehicle · Placeable 3D Effectors (Sphere + Box Volumes Sculpting Car Surface!)
    {
        StrandCameraSpec Cam {};
        Cam.FocusMeters[0] = 0.05f; Cam.FocusMeters[1] = 0.80f; Cam.FocusMeters[2] = 0.0f;
        Cam.YawRadians     = 0.72f;
        Cam.PitchRadians   = 0.30f;
        Cam.DistanceMeters = 7.75f;
        Cam.FocalMm        = 34.0f;
        ExecuteFrameCapture(
            "effector-object-placer",
            "05_ControlVehicle_PlaceableEffectors",
            "05 CONTROL VEHICLE : PLACEABLE 3D EFFECTORS A & B",
            Cam, 1.50f,
            [](ParticleDocument&) {});
    }

    // Panel 6: Effector Cube Matrix · Height Up/Down (24x24 Kinetic Ember Columns!)
    {
        StrandCameraSpec Cam {};
        Cam.FocusMeters[0] = 0.0f; Cam.FocusMeters[1] = 0.45f; Cam.FocusMeters[2] = 0.0f;
        Cam.YawRadians     = 0.68f;
        Cam.PitchRadians   = 0.48f;
        Cam.DistanceMeters = 6.90f;
        Cam.FocalMm        = 32.0f;
        ExecuteFrameCapture(
            "effector-grid-cubes",
            "06_EffectorCubeMatrix_HeightUpDown",
            "06 EFFECTOR CUBE MATRIX : HEIGHT UP/DOWN (24X24)",
            Cam, 1.90f,
            [](ParticleDocument& Doc) {
                auto& Rig = Doc.Rigs[0];
                if (auto* Lat = std::get_if<SurfaceLatticeOrigin>(&Rig.Origin))
                {
                    Lat->CenterMeters[0] = 0.0f;
                    Lat->CenterMeters[1] = 0.04f;
                    Lat->CenterMeters[2] = 0.0f;
                }
            });
    }

    // Stitch crisp 3x2 proof sheet (2220 x 1196)
    constexpr int Cols    = 3;
    constexpr int Rows    = 2;
    constexpr int Pad     = 16;
    constexpr int HeadH   = 44;
    constexpr int CapH    = 28;
    const int SheetW      = Pad + Cols * (RenderW + Pad);
    const int SheetH      = HeadH + Pad + Rows * (RenderH + CapH + Pad);
    std::vector<uint8_t> Sheet(static_cast<std::size_t>(SheetW * SheetH * 4), 255u);
    FillRectRgba8(Sheet, SheetW, SheetH, 0, 0, SheetW, SheetH, 10u, 13u, 19u);
    FillRectRgba8(Sheet, SheetW, SheetH, 0, 0, SheetW, HeadH, 17u, 23u, 34u);
    BlendBannerText(
        Sheet, SheetW, SheetH, 20, 15,
        "FRONTIER C++ MOTION & GRIDS : CONTROLLED INTEGRATOR (CONTROLVEHICLE) + KINETIC HEX TORUS MATRIX",
        226u, 238u, 252u);

    for (std::size_t I = 0u; I < Panels.size(); ++I)
    {
        const int Col = static_cast<int>(I) % Cols;
        const int Row = static_cast<int>(I) / Cols;
        const int X0  = Pad + Col * (RenderW + Pad);
        const int Y0  = HeadH + Pad + Row * (RenderH + CapH + Pad);

        FillRectRgba8(Sheet, SheetW, SheetH, X0 - 2, Y0 - 2, RenderW + 4, RenderH + CapH + 4, 38u, 52u, 74u);
        FillRectRgba8(Sheet, SheetW, SheetH, X0, Y0 + RenderH, RenderW, CapH, 16u, 22u, 32u);

        const auto& P = Panels[I];
        for (int Y = 0; Y < P.Height; ++Y)
        {
            for (int X = 0; X < P.Width; ++X)
            {
                const std::size_t Src = static_cast<std::size_t>((Y * P.Width + X) * 4);
                const std::size_t Dst = static_cast<std::size_t>(((Y0 + Y) * SheetW + (X0 + X)) * 4);
                Sheet[Dst + 0u] = P.Rgba[Src + 0u];
                Sheet[Dst + 1u] = P.Rgba[Src + 1u];
                Sheet[Dst + 2u] = P.Rgba[Src + 2u];
                Sheet[Dst + 3u] = 255u;
            }
        }
        BlendBannerText(Sheet, SheetW, SheetH, X0 + 10, Y0 + RenderH + 7, P.Caption.c_str(), 186u, 230u, 253u);
    }

    const auto ProofPath = OutDir / "MotionGrid_ControlVehicle_Proof.png";
    const int WroteOk = stbi_write_png(ProofPath.string().c_str(), SheetW, SheetH, 4, Sheet.data(), SheetW * 4);
    ExpectCheck(WroteOk != 0, "Wrote MotionGrid_ControlVehicle_Proof.png successfully");

    std::cout << "=== ParticleMotionGridValidation Complete: failures=" << gFailures << " ===\n";
    return (gFailures == 0) ? 0 : 1;
}
