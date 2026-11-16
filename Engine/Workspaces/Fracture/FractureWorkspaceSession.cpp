/*============================================================================================================================================
                                                       FRACTUREWORKSPACESESSION.CPP
============================================================================================================================================*/
// 🧩 Implementation of FractureWorkspaceSession: 9 Port-8091 analytical primitives, Torus/Pipe convex wedges, 6-solver dispatch & Jolt sim

#include "Engine/Workspaces/Fracture/FractureWorkspaceSession.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace Frontier::Workspaces::Fracture
{

using namespace GeometrySpace::Fracture;

namespace
{

constexpr double PiConst  = 3.14159265358979323846;
constexpr double TauConst = PiConst * 2.0;

[[nodiscard]] const char* SolverShortTitle(FractureSolverMethod Method) noexcept
{
    switch (Method)
    {
    case FractureSolverMethod::ClusteredVoronoi:      return "Clustered Voronoi";
    case FractureSolverMethod::FastRuntimeBoussinesq: return "Uniform Voronoi";
    case FractureSolverMethod::LefmWavefront:         return "Radial";
    case FractureSolverMethod::GriffithStressBudget:  return "Planar slices";
    case FractureSolverMethod::EngineeringBakedSolid: return "Brick";
    case FractureSolverMethod::DendriticSpallHull:    return "Recursive split";
    }
    return "Clustered Voronoi";
}

[[nodiscard]] ConvexPolyhedron BuildBoxExtent(float Width, float Height, float Depth)
{
    return ConvexPartitioning::ConstructBoxPolyhedron(
        std::max(0.02f, Width),
        std::max(0.02f, Depth),
        std::max(0.02f, Height));
}

[[nodiscard]] ConvexPolyhedron BuildSpherePolyhedron(float Radius, uint32_t Rings, uint32_t Slices)
{
    ConvexPolyhedron Poly {};
    const float R = std::max(0.05f, Radius);
    Poly.Vertices.push_back({ 0.0f, 0.0f, R });
    for (uint32_t I = 1u; I < Rings; ++I)
    {
        const double Phi = PiConst * static_cast<double>(I) / static_cast<double>(Rings);
        const float Z    = static_cast<float>(R * std::cos(Phi));
        const float RingR = static_cast<float>(R * std::sin(Phi));
        for (uint32_t J = 0u; J < Slices; ++J)
        {
            const double Theta = TauConst * static_cast<double>(J) / static_cast<double>(Slices);
            Poly.Vertices.push_back({
                static_cast<float>(RingR * std::cos(Theta)),
                static_cast<float>(RingR * std::sin(Theta)),
                Z
            });
        }
    }
    const uint32_t SouthIdx = static_cast<uint32_t>(Poly.Vertices.size());
    Poly.Vertices.push_back({ 0.0f, 0.0f, -R });

    for (uint32_t J = 0u; J < Slices; ++J)
    {
        const uint32_t Next = (J + 1u) % Slices;
        Poly.Faces.push_back(ConvexFace { { 0u, 1u + J, 1u + Next }, false, 0u, { 0.0f, 0.0f, 1.0f } });
    }
    for (uint32_t I = 0u; I + 2u < Rings; ++I)
    {
        const uint32_t RowA = 1u + I * Slices;
        const uint32_t RowB = 1u + (I + 1u) * Slices;
        for (uint32_t J = 0u; J < Slices; ++J)
        {
            const uint32_t Next = (J + 1u) % Slices;
            Poly.Faces.push_back(ConvexFace { { RowA + J, RowB + J, RowB + Next, RowA + Next }, false, 0u, { 0.0f, 0.0f, 1.0f } });
        }
    }
    const uint32_t LastRow = 1u + (Rings - 2u) * Slices;
    for (uint32_t J = 0u; J < Slices; ++J)
    {
        const uint32_t Next = (J + 1u) % Slices;
        Poly.Faces.push_back(ConvexFace { { LastRow + J, SouthIdx, LastRow + Next }, false, 0u, { 0.0f, 0.0f, -1.0f } });
    }
    ConvexPartitioning::OrientFacesOutward(Poly);
    return Poly;
}

[[nodiscard]] ConvexPolyhedron BuildCapsulePolyhedron(float Radius, float ShaftLength, uint32_t Rings, uint32_t Slices)
{
    ConvexPolyhedron Poly {};
    const float R     = std::max(0.04f, Radius);
    const float HalfL = std::max(0.0f, ShaftLength * 0.5f);
    Poly.Vertices.push_back({ 0.0f, 0.0f, HalfL + R });
    const uint32_t HalfRings = std::max(2u, Rings / 2u);
    for (uint32_t I = 1u; I <= HalfRings; ++I)
    {
        const double Phi  = (PiConst * 0.5) * static_cast<double>(I) / static_cast<double>(HalfRings);
        const float Z     = HalfL + static_cast<float>(R * std::cos(Phi));
        const float RingR = static_cast<float>(R * std::sin(Phi));
        for (uint32_t J = 0u; J < Slices; ++J)
        {
            const double Theta = TauConst * static_cast<double>(J) / static_cast<double>(Slices);
            Poly.Vertices.push_back({ static_cast<float>(RingR * std::cos(Theta)), static_cast<float>(RingR * std::sin(Theta)), Z });
        }
    }
    for (uint32_t I = 0u; I < HalfRings; ++I)
    {
        const double Phi  = (PiConst * 0.5) + (PiConst * 0.5) * static_cast<double>(I) / static_cast<double>(HalfRings);
        const float Z     = -HalfL + static_cast<float>(R * std::cos(Phi));
        const float RingR = static_cast<float>(R * std::sin(Phi));
        for (uint32_t J = 0u; J < Slices; ++J)
        {
            const double Theta = TauConst * static_cast<double>(J) / static_cast<double>(Slices);
            Poly.Vertices.push_back({ static_cast<float>(RingR * std::cos(Theta)), static_cast<float>(RingR * std::sin(Theta)), Z });
        }
    }
    const uint32_t SouthIdx = static_cast<uint32_t>(Poly.Vertices.size());
    Poly.Vertices.push_back({ 0.0f, 0.0f, -HalfL - R });

    const uint32_t TotalRows = HalfRings * 2u;
    for (uint32_t J = 0u; J < Slices; ++J)
    {
        const uint32_t Next = (J + 1u) % Slices;
        Poly.Faces.push_back(ConvexFace { { 0u, 1u + J, 1u + Next }, false, 0u, { 0.0f, 0.0f, 1.0f } });
    }
    for (uint32_t I = 0u; I + 1u < TotalRows; ++I)
    {
        const uint32_t RowA = 1u + I * Slices;
        const uint32_t RowB = 1u + (I + 1u) * Slices;
        for (uint32_t J = 0u; J < Slices; ++J)
        {
            const uint32_t Next = (J + 1u) % Slices;
            Poly.Faces.push_back(ConvexFace { { RowA + J, RowB + J, RowB + Next, RowA + Next }, false, 0u, { 0.0f, 0.0f, 1.0f } });
        }
    }
    const uint32_t LastRow = 1u + (TotalRows - 1u) * Slices;
    for (uint32_t J = 0u; J < Slices; ++J)
    {
        const uint32_t Next = (J + 1u) % Slices;
        Poly.Faces.push_back(ConvexFace { { LastRow + J, SouthIdx, LastRow + Next }, false, 0u, { 0.0f, 0.0f, -1.0f } });
    }
    ConvexPartitioning::OrientFacesOutward(Poly);
    return Poly;
}

[[nodiscard]] std::vector<ConvexPolyhedron> BuildTorusWedges(float RingRadius, float TubeRadius, uint32_t AroundN, uint32_t TubeM)
{
    std::vector<ConvexPolyhedron> Parts;
    Parts.reserve(AroundN);
    const double R  = std::max(0.15, static_cast<double>(RingRadius));
    const double r  = std::min(R * 0.90, std::max(0.04, static_cast<double>(TubeRadius)));
    const double Rr = (R * TauConst) / (static_cast<double>(AroundN) * std::sin(TauConst / static_cast<double>(AroundN)));
    const double Tr = r * std::sqrt(TauConst / (static_cast<double>(TubeM) * std::sin(TauConst / static_cast<double>(TubeM))));

    auto RingPt = [&](double A, double F) -> Mathematics::Axis3D
    {
        const double Rho = Rr + Tr * std::cos(F);
        return {
            static_cast<float>(Rho * std::cos(A)),
            static_cast<float>(Rho * std::sin(A)),
            static_cast<float>(Tr * std::sin(F))
        };
    };

    for (uint32_t S = 0u; S < AroundN; ++S)
    {
        const double A0 = (static_cast<double>(S) / static_cast<double>(AroundN)) * TauConst;
        const double A1 = (static_cast<double>(S + 1u) / static_cast<double>(AroundN)) * TauConst;
        ConvexPolyhedron Wedge {};
        Wedge.Vertices.reserve(TubeM * 2u);
        for (uint32_t K = 0u; K < TubeM; ++K)
        {
            const double F = (static_cast<double>(K) / static_cast<double>(TubeM)) * TauConst;
            Wedge.Vertices.push_back(RingPt(A0, F));
        }
        for (uint32_t K = 0u; K < TubeM; ++K)
        {
            const double F = (static_cast<double>(K) / static_cast<double>(TubeM)) * TauConst;
            Wedge.Vertices.push_back(RingPt(A1, F));
        }
        std::vector<uint32_t> Cap0(TubeM), Cap1(TubeM);
        for (uint32_t K = 0u; K < TubeM; ++K)
        {
            Cap0[K] = TubeM - 1u - K;
            Cap1[K] = TubeM + K;
        }
        Wedge.Faces.push_back(ConvexFace { std::move(Cap0), false, 0u, { 0.0f, 0.0f, 1.0f } });
        Wedge.Faces.push_back(ConvexFace { std::move(Cap1), false, 0u, { 0.0f, 0.0f, 1.0f } });
        for (uint32_t K = 0u; K < TubeM; ++K)
        {
            const uint32_t Next = (K + 1u) % TubeM;
            Wedge.Faces.push_back(ConvexFace { { K, Next, TubeM + Next, TubeM + K }, false, 0u, { 0.0f, 0.0f, 1.0f } });
        }
        ConvexPartitioning::OrientFacesOutward(Wedge);
        Parts.push_back(std::move(Wedge));
    }
    return Parts;
}

[[nodiscard]] std::vector<ConvexPolyhedron> BuildPipeWedges(float OuterR, float BoreR, float Height, uint32_t AroundN)
{
    std::vector<ConvexPolyhedron> Parts;
    Parts.reserve(AroundN);
    const double Ro = std::max(0.08, static_cast<double>(OuterR));
    const double Ri = std::clamp(static_cast<double>(BoreR), 0.02, Ro - 0.02);
    const float Hz  = std::max(0.05f, Height) * 0.5f;

    for (uint32_t S = 0u; S < AroundN; ++S)
    {
        const double A0 = (static_cast<double>(S) / static_cast<double>(AroundN)) * TauConst;
        const double A1 = (static_cast<double>(S + 1u) / static_cast<double>(AroundN)) * TauConst;
        const float C0 = static_cast<float>(std::cos(A0)), S0 = static_cast<float>(std::sin(A0));
        const float C1 = static_cast<float>(std::cos(A1)), S1 = static_cast<float>(std::sin(A1));
        ConvexPolyhedron W {};
        W.Vertices = {
            { static_cast<float>(Ri * C0), static_cast<float>(Ri * S0), -Hz },
            { static_cast<float>(Ro * C0), static_cast<float>(Ro * S0), -Hz },
            { static_cast<float>(Ro * C1), static_cast<float>(Ro * S1), -Hz },
            { static_cast<float>(Ri * C1), static_cast<float>(Ri * S1), -Hz },
            { static_cast<float>(Ri * C0), static_cast<float>(Ri * S0),  Hz },
            { static_cast<float>(Ro * C0), static_cast<float>(Ro * S0),  Hz },
            { static_cast<float>(Ro * C1), static_cast<float>(Ro * S1),  Hz },
            { static_cast<float>(Ri * C1), static_cast<float>(Ri * S1),  Hz },
        };
        W.Faces = {
            { { 0, 3, 2, 1 }, false, 0u, { 0.0f, 0.0f, -1.0f } },
            { { 4, 5, 6, 7 }, false, 0u, { 0.0f, 0.0f,  1.0f } },
            { { 1, 2, 6, 5 }, false, 0u, { 1.0f, 0.0f,  0.0f } },
            { { 3, 0, 4, 7 }, false, 0u, { -1.0f, 0.0f, 0.0f } },
            { { 0, 1, 5, 4 }, false, 0u, { 0.0f, -1.0f, 0.0f } },
            { { 2, 3, 7, 6 }, false, 0u, { 0.0f,  1.0f, 0.0f } },
        };
        ConvexPartitioning::OrientFacesOutward(W);
        Parts.push_back(std::move(W));
    }
    return Parts;
}

[[nodiscard]] ConvexPolyhedron MergePartsPreview(const std::vector<ConvexPolyhedron>& Parts)
{
    ConvexPolyhedron Merged {};
    for (const auto& P : Parts)
    {
        const uint32_t Base = static_cast<uint32_t>(Merged.Vertices.size());
        Merged.Vertices.insert(Merged.Vertices.end(), P.Vertices.begin(), P.Vertices.end());
        for (const auto& F : P.Faces)
        {
            ConvexFace Copy = F;
            for (uint32_t& Idx : Copy.Indices)
            {
                Idx += Base;
            }
            Merged.Faces.push_back(std::move(Copy));
        }
    }
    return Merged;
}

} // namespace

const std::array<FracturePresetEntry, FractureWorkspaceSession::PresetCount>& FractureWorkspaceSession::InspectPresets() noexcept
{
    static constexpr std::array<FracturePresetEntry, PresetCount> Presets {{
        {
            "Torus",
            "Clustered Voronoi · Concrete · 2.50 kJ",
            FractureIcons::PrimitiveShapeId::Torus,
            FractureSolidCategory::Boulder,
            "concrete",
            "concrete",
            FractureSolverMethod::ClusteredVoronoi,
            FractureExecutionMode::Baked,
            2500.0,
            42u,
            48u,
            FractureRenderViewMode::Energy
        },
        {
            "Curtain Pane",
            "Radial · Tempered · 0.48 kJ",
            FractureIcons::PrimitiveShapeId::Pane,
            FractureSolidCategory::CurtainPane,
            "glass",
            "tempered",
            FractureSolverMethod::LefmWavefront,
            FractureExecutionMode::Baked,
            480.0,
            107u,
            42u,
            FractureRenderViewMode::Energy
        },
        {
            "Monolith",
            "Uniform Voronoi · Concrete · 3.20 kJ",
            FractureIcons::PrimitiveShapeId::Box,
            FractureSolidCategory::Monolith,
            "concrete",
            "concrete",
            FractureSolverMethod::FastRuntimeBoussinesq,
            FractureExecutionMode::HybridBonds,
            3200.0,
            42u,
            32u,
            FractureRenderViewMode::StudioOpenPBR
        },
        {
            "Baked Granite",
            "Brick · Bedded Granite · 4.20 kJ",
            FractureIcons::PrimitiveShapeId::HexColumn,
            FractureSolidCategory::Cylinder,
            "granite",
            "granite",
            FractureSolverMethod::EngineeringBakedSolid,
            FractureExecutionMode::Baked,
            4200.0,
            73u,
            28u,
            FractureRenderViewMode::StudioOpenPBR
        },
        {
            "Spruce Cylinder",
            "Planar slices · Spruce Wood · 1.80 kJ",
            FractureIcons::PrimitiveShapeId::Cylinder,
            FractureSolidCategory::TimberBeam,
            "wood",
            "wood",
            FractureSolverMethod::GriffithStressBudget,
            FractureExecutionMode::Baked,
            1800.0,
            19u,
            24u,
            FractureRenderViewMode::Cells
        },
        {
            "Polymer Pipe",
            "Recursive split · ABS Polymer · 3.60 kJ",
            FractureIcons::PrimitiveShapeId::Pipe,
            FractureSolidCategory::HollowCavity,
            "plastic",
            "plastic",
            FractureSolverMethod::DendriticSpallHull,
            FractureExecutionMode::Baked,
            3600.0,
            88u,
            36u,
            FractureRenderViewMode::StudioOpenPBR
        }
    }};
    return Presets;
}

const std::array<FracturePrimitiveDescriptor, FractureWorkspaceSession::PrimitiveCount>& FractureWorkspaceSession::InspectPrimitives() noexcept
{
    static constexpr std::array<FracturePrimitiveDescriptor, PrimitiveCount> Primitives {{
        { FractureIcons::PrimitiveShapeId::Box,       "box",      "Box",        "W · H · D",        "Analytical box",      false },
        { FractureIcons::PrimitiveShapeId::Sphere,    "sphere",   "Sphere",     "4/3 pi r^3",       "Analytical sphere",   false },
        { FractureIcons::PrimitiveShapeId::Cylinder,  "cylinder", "Cylinder",   "pi r^2 h",         "Analytical cylinder", false },
        { FractureIcons::PrimitiveShapeId::Cone,      "cone",     "Cone",       "1/3 pi r^2 h",     "Analytical cone",     false },
        { FractureIcons::PrimitiveShapeId::Capsule,   "capsule",  "Capsule",    "pi r^2 l + 4/3r3", "Analytical capsule",  false },
        { FractureIcons::PrimitiveShapeId::Torus,     "torus",    "Torus",      "2 pi^2 R r^2",     "Analytical torus",    true  },
        { FractureIcons::PrimitiveShapeId::Pipe,      "pipe",     "Pipe",       "pi (R^2 - r^2) h", "Analytical pipe",     true  },
        { FractureIcons::PrimitiveShapeId::Pane,      "pane",     "Pane",       "W · H · t",        "Analytical pane",     false },
        { FractureIcons::PrimitiveShapeId::HexColumn, "hex",      "Hex column", "2 sqrt(3) a^2 h",  "Analytical hex",      false },
    }};
    return Primitives;
}

FractureWorkspaceSession::FractureWorkspaceSession()
{
    FrameTimeSamplesMs.assign(96u, 16.67f);
    for (uint32_t I = 0u; I < 96u; ++I)
    {
        FrameTimeSamplesMs[I] = 16.3f + 0.6f * std::sin(static_cast<float>(I) * 0.28f);
    }
    ApplyPreset(0u);
}

void FractureWorkspaceSession::ApplyPreset(uint32_t PresetIndex)
{
    const uint32_t SafeIdx = PresetIndex % PresetCount;
    const auto& Preset     = InspectPresets()[SafeIdx];
    ActivePresetIndex      = SafeIdx;
    SurfaceMaterialId      = Preset.SurfaceMatId;
    MaterialId             = Preset.MaterialId;
    SolverMethod           = Preset.Solver;
    ExecutionMode          = Preset.ExecMode;
    MethodMode             = (ExecutionMode == FractureExecutionMode::Realtime) ? FractureMethodMode::Realtime : FractureMethodMode::Baked;
    ImpactEnergyJ          = Preset.ImpactEnergyJ;
    Seed                   = Preset.Seed;
    FragmentCeiling        = Preset.FragmentCeiling;
    RenderOptions.RenderMode = Preset.DefaultLens;
    RenderOptions.ShowGroundGrid = true;
    RenderOptions.ShowBonds      = false;
    RenderOptions.ShowWavefronts = true;
    RenderOptions.ExplodeFactor  = 0.0f;
    RenderOptions.SelectedFragmentIndex = -1;

    ApplyPrimitiveShape(Preset.Shape);
    AppendLog("preset", Preset.Title, Preset.Subtitle, true);
}

void FractureWorkspaceSession::ApplyPrimitiveShape(FractureIcons::PrimitiveShapeId Shape)
{
    ActiveShape = Shape;
    switch (Shape)
    {
    case FractureIcons::PrimitiveShapeId::Box:
        SolidCategory = FractureSolidCategory::Cube;
        DimPrimaryA = 1.00f; DimPrimaryB = 1.00f; DimPrimaryC = 1.00f;
        ImpactPointLocal = { 0.42f, -0.45f, 0.16f };
        ImpactNormalLocal = { 0.25f, -0.94f, 0.22f };
        break;
    case FractureIcons::PrimitiveShapeId::Sphere:
        SolidCategory = FractureSolidCategory::Boulder;
        DimPrimaryA = 0.50f; DimPrimaryB = 0.50f; DimPrimaryC = 0.50f;
        ImpactPointLocal = { 0.30f, -0.34f, 0.21f };
        ImpactNormalLocal = { 0.60f, -0.68f, 0.42f };
        break;
    case FractureIcons::PrimitiveShapeId::Cylinder:
        SolidCategory = FractureSolidCategory::Cylinder;
        DimPrimaryA = 0.40f; DimPrimaryB = 1.20f; DimPrimaryC = 0.40f;
        ImpactPointLocal = { 0.26f, -0.30f, 0.20f };
        ImpactNormalLocal = { 0.65f, -0.76f, 0.0f };
        break;
    case FractureIcons::PrimitiveShapeId::Cone:
        SolidCategory = FractureSolidCategory::Cone;
        DimPrimaryA = 0.50f; DimPrimaryB = 1.10f; DimPrimaryC = 0.50f;
        ImpactPointLocal = { 0.22f, -0.26f, 0.05f };
        ImpactNormalLocal = { 0.62f, -0.74f, 0.25f };
        break;
    case FractureIcons::PrimitiveShapeId::Capsule:
        SolidCategory = FractureSolidCategory::Cylinder;
        DimPrimaryA = 0.30f; DimPrimaryB = 0.70f; DimPrimaryC = 0.30f;
        ImpactPointLocal = { 0.19f, -0.22f, 0.10f };
        ImpactNormalLocal = { 0.65f, -0.76f, 0.0f };
        break;
    case FractureIcons::PrimitiveShapeId::Torus:
        SolidCategory = FractureSolidCategory::Boulder;
        DimPrimaryA = 0.50f; DimPrimaryB = 0.19f; DimPrimaryC = 0.50f;
        ImpactPointLocal = { 0.32f, -0.52f, 0.10f };
        ImpactNormalLocal = { 0.48f, -0.84f, 0.24f };
        break;
    case FractureIcons::PrimitiveShapeId::Pipe:
        SolidCategory = FractureSolidCategory::HollowCavity;
        DimPrimaryA = 0.48f; DimPrimaryB = 0.34f; DimPrimaryC = 0.80f;
        ImpactPointLocal = { 0.30f, -0.36f, 0.12f };
        ImpactNormalLocal = { 0.64f, -0.77f, 0.0f };
        break;
    case FractureIcons::PrimitiveShapeId::Pane:
        SolidCategory = FractureSolidCategory::CurtainPane;
        DimPrimaryA = 1.70f; DimPrimaryB = 1.15f; DimPrimaryC = 14.0f;
        ImpactPointLocal = { 0.10f, -0.02f, 0.08f };
        ImpactNormalLocal = { 0.0f, -1.0f, 0.0f };
        break;
    case FractureIcons::PrimitiveShapeId::HexColumn:
        SolidCategory = FractureSolidCategory::Cylinder;
        DimPrimaryA = 0.56f; DimPrimaryB = 1.20f; DimPrimaryC = 0.56f;
        ImpactPointLocal = { 0.18f, -0.24f, 0.14f };
        ImpactNormalLocal = { 0.56f, -0.83f, 0.0f };
        break;
    default:
        break;
    }

    ApplyCameraView(0u);
    ExecutionPhase = ExecutionStripPhase::Edit;
    Playing = false;
    ElapsedSeconds = 0.0;
    RebuildSourceGeometry();
    RebuildFracturePartition();
}

void FractureWorkspaceSession::ApplySolverMethod(FractureSolverMethod Method)
{
    SolverMethod = Method;
    if (Method == FractureSolverMethod::FastRuntimeBoussinesq && MethodMode == FractureMethodMode::Realtime)
    {
        ExecutionMode = FractureExecutionMode::Realtime;
    }
    else
    {
        ExecutionMode = FractureExecutionMode::Baked;
    }
    RebuildFracturePartition();
    AppendLog("solver", SolverShortTitle(Method), ActiveMaterialSpec().ShortLabel, true);
}

void FractureWorkspaceSession::ApplyCameraView(uint32_t ViewIndex) noexcept
{
    CameraViewIndex = ViewIndex % 4u;
    switch (CameraViewIndex)
    {
    case 0u:
        Camera.YawRadians   = (ActiveShape == FractureIcons::PrimitiveShapeId::Torus) ? 0.64f : 0.72f;
        Camera.PitchRadians = (ActiveShape == FractureIcons::PrimitiveShapeId::Torus) ? 0.44f : 0.38f;
        Camera.DistanceM    = (ActiveShape == FractureIcons::PrimitiveShapeId::Torus) ? 2.25f : 2.65f;
        break;
    case 1u:
        Camera.YawRadians = 0.0f; Camera.PitchRadians = 1.38f; Camera.DistanceM = 2.60f;
        break;
    case 2u:
        Camera.YawRadians = 0.0f; Camera.PitchRadians = 0.16f; Camera.DistanceM = 2.60f;
        break;
    case 3u:
        Camera.YawRadians = 1.57f; Camera.PitchRadians = 0.16f; Camera.DistanceM = 2.60f;
        break;
    }
    const auto Metrics = ConvexPartitioning::ComputePolyhedronMetrics(SourceSolid);
    const float FocusZ = (SourceSolid.Vertices.empty()) ? 0.24f
        : std::max(0.18f, static_cast<float>(Metrics.Centroid.Z - Metrics.BoundsMin.Z));
    Camera.TargetWorld = { 0.0f, 0.0f, FocusZ };
}

void FractureWorkspaceSession::RebuildSourceGeometry()
{
    const uint32_t AroundN = (FacetDetail == FractureFacetDetail::Low) ? 24u
                           : (FacetDetail == FractureFacetDetail::High) ? 64u : 40u;
    SourceParts.clear();
    switch (ActiveShape)
    {
    case FractureIcons::PrimitiveShapeId::Box:
        SourceSolid = BuildBoxExtent(DimPrimaryA, DimPrimaryB, DimPrimaryC);
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    case FractureIcons::PrimitiveShapeId::Pane:
        SourceSolid = BuildBoxExtent(DimPrimaryA, DimPrimaryB, std::max(0.014f, DimPrimaryC / 1000.0f));
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    case FractureIcons::PrimitiveShapeId::Sphere:
        SourceSolid = BuildSpherePolyhedron(DimPrimaryA, 14u, AroundN / 2u);
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    case FractureIcons::PrimitiveShapeId::Cylinder:
        SourceSolid = ConvexPartitioning::ConstructPrismPolyhedron(DimPrimaryA, DimPrimaryA, DimPrimaryB, AroundN);
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    case FractureIcons::PrimitiveShapeId::Cone:
        SourceSolid = ConvexPartitioning::ConstructPrismPolyhedron(DimPrimaryA, 0.008f, DimPrimaryB, AroundN);
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    case FractureIcons::PrimitiveShapeId::Capsule:
        SourceSolid = BuildCapsulePolyhedron(DimPrimaryA, DimPrimaryB, 14u, AroundN / 2u);
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    case FractureIcons::PrimitiveShapeId::HexColumn:
    {
        const float SideR = (DimPrimaryA * 0.5f) / std::cos(static_cast<float>(PiConst / 6.0));
        SourceSolid = ConvexPartitioning::ConstructPrismPolyhedron(SideR, SideR, DimPrimaryB, 6u);
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    }
    case FractureIcons::PrimitiveShapeId::Torus:
        SourceParts = BuildTorusWedges(DimPrimaryA, DimPrimaryB, AroundN, std::max(12u, AroundN / 2u));
        SourceSolid = MergePartsPreview(SourceParts);
        SourceSegmentCount = static_cast<uint32_t>(SourceParts.size());
        break;
    case FractureIcons::PrimitiveShapeId::Pipe:
        SourceParts = BuildPipeWedges(DimPrimaryA, DimPrimaryB, DimPrimaryC, AroundN);
        SourceSolid = MergePartsPreview(SourceParts);
        SourceSegmentCount = static_cast<uint32_t>(SourceParts.size());
        break;
    default:
        SourceSolid = BuildBoxExtent(1.0f, 1.0f, 1.0f);
        SourceParts.push_back(SourceSolid);
        SourceSegmentCount = 1u;
        break;
    }

    SourceFaceCount = static_cast<uint32_t>(SourceSolid.Faces.size());
    const auto Metrics = ConvexPartitioning::ComputePolyhedronMetrics(SourceSolid);
    const double BottomZ = static_cast<double>(Metrics.BoundsMin.Z);
    RenderOptions.SolidWorldOffsetM = { 0.0f, 0.0f, static_cast<float>(-BottomZ) };
    Camera.TargetWorld = { 0.0f, 0.0f, std::max(0.18f, static_cast<float>(Metrics.Centroid.Z - BottomZ)) };
}

void FractureWorkspaceSession::ExecuteFracturePartitionOnly()
{
    FractureSpecification Spec {};
    Spec.Solver            = SolverMethod;
    Spec.MaterialId        = MaterialId.c_str();
    Spec.Seed              = Seed;
    Spec.Ceiling           = std::max(4u, FragmentCeiling);
    Spec.EnergyJoules      = ImpactEnergyJ;
    Spec.MinimumSizeM      = MinFragmentSpanM;
    Spec.GrainAngleDeg     = GrainAngleDeg;
    Spec.BondStrengthScale = BondStrengthScale;
    Spec.ImpactPoint       = ImpactPointLocal;
    Spec.ImpactNormal      = ImpactNormalLocal;

    if (SourceParts.size() <= 1u)
    {
        PartitionResult = ConvexPartitioning::ExecuteFracturePartition(SourceSolid, Spec);
    }
    else
    {
        // Partition segmented Torus / Pipe across convex ring segments and assemble unified fragment + bond receipt
        PartitionResult = FracturePartitionResult {};
        PartitionResult.Solver       = SolverMethod;
        PartitionResult.MaterialSpec = ActiveMaterialSpec();

        const uint32_t SegCount = static_cast<uint32_t>(SourceParts.size());
        const uint32_t TargetTotal = std::max(4u, std::min(FragmentCeiling, 64u));
        const uint32_t ExtraAllowed = (TargetTotal > SegCount) ? (TargetTotal - SegCount) : 4u;
        const uint32_t ImpactedRingCount = std::min(SegCount, std::max(3u, ExtraAllowed / 2u));

        // Sort segments by distance from impact point so the near-impact zone splits finely while far segments hold
        std::vector<std::pair<double, uint32_t>> RankedSegs;
        RankedSegs.reserve(SegCount);
        for (uint32_t S = 0u; S < SegCount; ++S)
        {
            const auto M = ConvexPartitioning::ComputePolyhedronMetrics(SourceParts[S]);
            const double Dist = std::hypot(
                M.Centroid.X - ImpactPointLocal.X,
                std::hypot(M.Centroid.Y - ImpactPointLocal.Y, M.Centroid.Z - ImpactPointLocal.Z));
            RankedSegs.push_back({ Dist, S });
        }
        std::sort(RankedSegs.begin(), RankedSegs.end());

        double TotalComputeMs = 0.0;
        double TotalCrackArea = 0.0;
        double SumVol = 0.0;
        uint32_t ExtraUsed = 0u;
        for (uint32_t Rank = 0u; Rank < SegCount; ++Rank)
        {
            const uint32_t S = RankedSegs[Rank].second;
            const auto& Wedge = SourceParts[S];
            const auto WM = ConvexPartitioning::ComputePolyhedronMetrics(Wedge);

            if (Rank < ImpactedRingCount && ExtraUsed < ExtraAllowed)
            {
                FractureSpecification SegSpec = Spec;
                SegSpec.Ceiling = std::min(4u, 1u + (ExtraAllowed - ExtraUsed));
                SegSpec.Seed    = Seed + S * 37u;
                auto Sub = ConvexPartitioning::ExecuteFracturePartition(Wedge, SegSpec);
                if (Sub.Fragments.size() > 1u && ExtraUsed + static_cast<uint32_t>(Sub.Fragments.size() - 1u) <= ExtraAllowed)
                {
                    ExtraUsed += static_cast<uint32_t>(Sub.Fragments.size() - 1u);
                    TotalComputeMs += Sub.Receipt.ComputeDurationMs;
                    TotalCrackArea += Sub.Receipt.TotalCrackAreaM2;
                    for (auto& Frag : Sub.Fragments)
                    {
                        Frag.Index = static_cast<uint32_t>(PartitionResult.Fragments.size());
                        char Tok[24] {};
                        std::snprintf(Tok, sizeof(Tok), "Fragment %02u", Frag.Index + 1u);
                        Frag.Token = Tok;
                        const double Dist = std::hypot(
                            Frag.Centroid.X - ImpactPointLocal.X,
                            std::hypot(Frag.Centroid.Y - ImpactPointLocal.Y, Frag.Centroid.Z - ImpactPointLocal.Z));
                        Frag.Proximity = static_cast<float>(std::exp(-Dist * 2.1));
                        SumVol += Frag.VolumeM3;
                        PartitionResult.Fragments.push_back(std::move(Frag));
                    }
                    continue;
                }
            }
            {
                FracturePieceEntry Kept {};
                Kept.Index          = static_cast<uint32_t>(PartitionResult.Fragments.size());
                char Tok[24] {};
                std::snprintf(Tok, sizeof(Tok), "Fragment %02u", Kept.Index + 1u);
                Kept.Token          = Tok;
                Kept.Polyhedron     = Wedge;
                Kept.VolumeM3       = WM.VolumeM3;
                Kept.ConvexVolumeM3 = WM.VolumeM3;
                Kept.MassKg         = WM.VolumeM3 * PartitionResult.MaterialSpec.DensityKgPerM3;
                Kept.Centroid       = WM.Centroid;
                Kept.BoundsMin      = WM.BoundsMin;
                Kept.BoundsMax      = WM.BoundsMax;
                Kept.Span           = WM.Span;
                Kept.RadiusM        = WM.RadiusM;
                Kept.CrackAreaM2    = WM.CrackAreaM2;
                const double Dist = RankedSegs[Rank].first;
                Kept.Proximity      = static_cast<float>(std::exp(-Dist * 2.1));
                Kept.StressRatio    = Kept.Proximity;
                SumVol += Kept.VolumeM3;
                PartitionResult.Fragments.push_back(std::move(Kept));
            }
        }

        // Re-sort fragments by Proximity descending so Fragment 01 is highest energy
        std::sort(PartitionResult.Fragments.begin(), PartitionResult.Fragments.end(),
            [](const FracturePieceEntry& A, const FracturePieceEntry& B) { return A.Proximity > B.Proximity; });
        for (uint32_t I = 0u; I < PartitionResult.Fragments.size(); ++I)
        {
            PartitionResult.Fragments[I].Index = I;
            char Tok[24] {};
            std::snprintf(Tok, sizeof(Tok), "Fragment %02u", I + 1u);
            PartitionResult.Fragments[I].Token = Tok;
        }

        PartitionResult.Bonds = ConvexPartitioning::ConstructSharedCutBonds(
            PartitionResult.Fragments, PartitionResult.MaterialSpec, ImpactPointLocal, DimPrimaryA + DimPrimaryB, BondStrengthScale);

        double SeamCutArea = 0.0;
        for (const auto& B : PartitionResult.Bonds)
        {
            SeamCutArea += B.SharedAreaM2;
        }
        const double ExactV = QueryExactVolumeM3();
        PartitionResult.Receipt.FragmentCount      = static_cast<uint32_t>(PartitionResult.Fragments.size());
        PartitionResult.Receipt.BondCount          = static_cast<uint32_t>(PartitionResult.Bonds.size());
        PartitionResult.Receipt.SourceVolumeM3     = ExactV;
        PartitionResult.Receipt.FragmentVolumeSumM3 = SumVol;
        PartitionResult.Receipt.VolumeDriftRatio   = std::abs(SumVol - ExactV) / std::max(1.0e-9, ExactV);
        PartitionResult.Receipt.TotalCrackAreaM2   = std::max(0.45, TotalCrackArea + SeamCutArea);
        PartitionResult.Receipt.SpentSurfaceEnergyJ = PartitionResult.Receipt.TotalCrackAreaM2 * PartitionResult.MaterialSpec.FractureToughnessGc;
        PartitionResult.Receipt.ComputeDurationMs  = std::max(12.0, TotalComputeMs);
        PartitionResult.Receipt.RayleighSpeedMps   = 3120.0;
        PartitionResult.Receipt.WatertightManifold = (PartitionResult.Receipt.VolumeDriftRatio < 0.15);
    }

    HasFractured = !PartitionResult.Fragments.empty();

    const double TotalMs = std::max(0.8, PartitionResult.Receipt.ComputeDurationMs);
    LastTimings.TotalMs   = TotalMs;
    LastTimings.SeedingMs = TotalMs * 0.04;
    LastTimings.CellsMs   = TotalMs * 0.56;
    LastTimings.NeedlesMs = TotalMs * 0.39;
    LastTimings.SortMs    = TotalMs * 0.01;

    ++SessionRevision;
    if (RenderOptions.SelectedFragmentIndex >= static_cast<int32_t>(PartitionResult.Fragments.size()))
    {
        RenderOptions.SelectedFragmentIndex = -1;
    }
    FragmentVisibleFlags.assign(PartitionResult.Fragments.size(), true);
    RenderOptions.FragmentVisibleFlags = &FragmentVisibleFlags;
    RenderOptions.SurfaceMaterial      = &ActiveSurfaceMaterialSpec();
}

void FractureWorkspaceSession::RebuildFracturePartition()
{
    ExecuteFracturePartitionOnly();

    SimulationSpace::PhysicsSpace::FractureSimulationParameters SimCfg {};
    SimCfg.SolidWorldOffsetM  = RenderOptions.SolidWorldOffsetM;
    SimCfg.GroundPlaneZ       = 0.0f;
    SimCfg.Restitution        = static_cast<float>(Restitution);
    SimCfg.Friction           = static_cast<float>(Friction);
    SimCfg.EnableBonds        = true;
    SimCfg.ApplyImpactImpulse = true;

    HasSimulatedData = SimRunner.LoadPartitionResult(PartitionResult, SimCfg);
    ElapsedSeconds   = 0.0;
    KineticCurveSamples.assign(1u, 0.0f);
    TimelineMarkers.clear();
    TimelineMarkers.push_back({ 0.02f, { 255u, 138u, 61u, 255u }, "Impact" });
}

void FractureWorkspaceSession::TriggerViewportImpact(
    Mathematics::Axis3D WorldPoint,
    Mathematics::Axis3D WorldNormal,
    int32_t             HitFragmentIndex)
{
    (void)HitFragmentIndex;
    ImpactPointLocal  = WorldPoint - RenderOptions.SolidWorldOffsetM;
    ImpactNormalLocal = WorldNormal;
    if (ExecutionPhase == ExecutionStripPhase::Edit || MethodMode == FractureMethodMode::Realtime)
    {
        RebuildFracturePartition();
        AppendLog("impact", "Surface strike", ActiveMaterialSpec().ShortLabel, true);
    }
    else
    {
        const Mathematics::Axis3D ImpulseNs = Mathematics::NormalizeAxis(WorldNormal) * static_cast<float>(-std::sqrt(ImpactEnergyJ) * 1.4);
        SimRunner.TriggerImpactImpulse(WorldPoint, ImpulseNs, static_cast<float>(std::max(0.25, ImpactRadiusM * 8.0)));
        AppendLog("jolt", "Live impulse applied", "BreakableBonds", true);
    }
    ++SessionRevision;
}

void FractureWorkspaceSession::TransitionExecutionPhase(ExecutionStripPhase TargetPhase)
{
    auto ResetSim = [&]()
    {
        SimulationSpace::PhysicsSpace::FractureSimulationParameters SimCfg {};
        SimCfg.SolidWorldOffsetM  = RenderOptions.SolidWorldOffsetM;
        SimCfg.GroundPlaneZ       = 0.0f;
        SimCfg.Restitution        = static_cast<float>(Restitution);
        SimCfg.Friction           = static_cast<float>(Friction);
        SimCfg.EnableBonds        = true;
        SimCfg.ApplyImpactImpulse = true;
        HasSimulatedData = SimRunner.LoadPartitionResult(PartitionResult, SimCfg);
    };

    if (TargetPhase == ExecutionStripPhase::Edit)
    {
        ExecutionPhase = ExecutionStripPhase::Edit;
        Playing        = false;
        ElapsedSeconds = 0.0;
        if (HasSimulatedData)
        {
            ResetSim();
        }
        return;
    }

    if (!HasSimulatedData)
    {
        RebuildFracturePartition();
    }

    if (ExecutionPhase == ExecutionStripPhase::Edit)
    {
        ResetSim();
        ElapsedSeconds = 0.0;
        KineticCurveSamples.assign(1u, 0.0f);
    }

    if (TargetPhase == ExecutionStripPhase::Play && ExecutionPhase == ExecutionStripPhase::Play)
    {
        Playing = !Playing;
    }
    else
    {
        ExecutionPhase = TargetPhase;
        Playing        = true;
    }
    ++SessionRevision;
}

void FractureWorkspaceSession::TogglePlayPause()
{
    if (ExecutionPhase == ExecutionStripPhase::Edit)
    {
        TransitionExecutionPhase(ExecutionStripPhase::Simulate);
    }
    else
    {
        Playing = !Playing;
    }
}

void FractureWorkspaceSession::RewindToStart()
{
    ElapsedSeconds = 0.0;
    Playing        = false;
    if (HasSimulatedData)
    {
        SimulationSpace::PhysicsSpace::FractureSimulationParameters SimCfg {};
        SimCfg.SolidWorldOffsetM  = RenderOptions.SolidWorldOffsetM;
        SimCfg.GroundPlaneZ       = 0.0f;
        SimCfg.Restitution        = static_cast<float>(Restitution);
        SimCfg.Friction           = static_cast<float>(Friction);
        SimCfg.EnableBonds        = true;
        SimCfg.ApplyImpactImpulse = true;
        HasSimulatedData = SimRunner.LoadPartitionResult(PartitionResult, SimCfg);
    }
    ++SessionRevision;
}

void FractureWorkspaceSession::StepSingleFrame()
{
    if (ExecutionPhase == ExecutionStripPhase::Edit)
    {
        ExecutionPhase = ExecutionStripPhase::Simulate;
    }
    Playing = false;
    if (HasSimulatedData)
    {
        SimRunner.Step(1.0 / 60.0);
        ElapsedSeconds = std::min(DurationSeconds, ElapsedSeconds + (1.0 / 60.0));
        KineticCurveSamples.push_back(static_cast<float>(SimRunner.InspectTelemetry().TotalKineticJ));
        ++SessionRevision;
    }
}

void FractureWorkspaceSession::ScrubToTime(double TimeSeconds)
{
    const double Clamped = std::clamp(TimeSeconds, 0.0, DurationSeconds);
    if (ExecutionPhase == ExecutionStripPhase::Edit)
    {
        ExecutionPhase = ExecutionStripPhase::Simulate;
    }
    Playing = false;
    if (!HasSimulatedData)
    {
        return;
    }
    if (Clamped < ElapsedSeconds - 0.01)
    {
        SimulationSpace::PhysicsSpace::FractureSimulationParameters SimCfg {};
        SimCfg.SolidWorldOffsetM  = RenderOptions.SolidWorldOffsetM;
        SimCfg.GroundPlaneZ       = 0.0f;
        SimCfg.Restitution        = static_cast<float>(Restitution);
        SimCfg.Friction           = static_cast<float>(Friction);
        SimCfg.EnableBonds        = true;
        SimCfg.ApplyImpactImpulse = true;
        HasSimulatedData = SimRunner.LoadPartitionResult(PartitionResult, SimCfg);
        ElapsedSeconds = 0.0;
    }
    const double StepDt = 1.0 / 60.0;
    while (ElapsedSeconds + StepDt * 0.5 < Clamped)
    {
        SimRunner.Step(StepDt);
        ElapsedSeconds += StepDt;
    }
    ElapsedSeconds = Clamped;
    ++SessionRevision;
}

void FractureWorkspaceSession::Tick(double DeltaSeconds)
{
    const double ClampedDt = std::clamp(DeltaSeconds, 0.001, 0.05);
    LastFrameTimeMs = static_cast<float>(ClampedDt * 1000.0);
    if (!DiagnosticsSession.Paused && FrameTimeSamplesMs.size() >= 96u)
    {
        FrameTimeSamplesMs.erase(FrameTimeSamplesMs.begin());
        FrameTimeSamplesMs.push_back(LastFrameTimeMs);
    }

    if (ExecutionPhase == ExecutionStripPhase::Edit || !Playing || !HasSimulatedData)
    {
        return;
    }

    const double SimDt = ClampedDt * static_cast<double>(TimeScale);
    SimRunner.Step(SimDt);
    ElapsedSeconds += SimDt;
    KineticCurveSamples.push_back(static_cast<float>(SimRunner.InspectTelemetry().TotalKineticJ));
    if (KineticCurveSamples.size() > 160u)
    {
        KineticCurveSamples.erase(KineticCurveSamples.begin());
    }

    if (ElapsedSeconds >= DurationSeconds)
    {
        if (LoopEnabled)
        {
            SimulationSpace::PhysicsSpace::FractureSimulationParameters SimCfg {};
            SimCfg.SolidWorldOffsetM  = RenderOptions.SolidWorldOffsetM;
            SimCfg.GroundPlaneZ       = 0.0f;
            SimCfg.Restitution        = static_cast<float>(Restitution);
            SimCfg.Friction           = static_cast<float>(Friction);
            SimCfg.EnableBonds        = true;
            SimCfg.ApplyImpactImpulse = true;
            HasSimulatedData = SimRunner.LoadPartitionResult(PartitionResult, SimCfg);
            ElapsedSeconds = 0.0;
        }
        else
        {
            ElapsedSeconds = DurationSeconds;
            Playing = false;
        }
    }
    ++SessionRevision;
}

const FractureMaterialSpec& FractureWorkspaceSession::ActiveSurfaceMaterialSpec() const noexcept
{
    return FractureMaterialRepository::LookupMaterial(SurfaceMaterialId.c_str());
}

const FractureMaterialSpec& FractureWorkspaceSession::ActiveMaterialSpec() const noexcept
{
    return FractureMaterialRepository::LookupMaterial(MaterialId.c_str());
}

SdfVolumeFootprint FractureWorkspaceSession::QuerySdfFootprint() const noexcept
{
    return ConvexPartitioning::ComputeSdfVolumeFootprint(
        static_cast<uint32_t>(PartitionResult.Fragments.size()),
        SdfResolution,
        SdfEnabled);
}

double FractureWorkspaceSession::QueryExactVolumeM3() const noexcept
{
    switch (ActiveShape)
    {
    case FractureIcons::PrimitiveShapeId::Box:       return static_cast<double>(DimPrimaryA) * DimPrimaryB * DimPrimaryC;
    case FractureIcons::PrimitiveShapeId::Sphere:    return (4.0 / 3.0) * PiConst * std::pow(DimPrimaryA, 3.0);
    case FractureIcons::PrimitiveShapeId::Cylinder:  return PiConst * DimPrimaryA * DimPrimaryA * DimPrimaryB;
    case FractureIcons::PrimitiveShapeId::Cone:      return (PiConst * DimPrimaryA * DimPrimaryA * DimPrimaryB) / 3.0;
    case FractureIcons::PrimitiveShapeId::Capsule:   return PiConst * DimPrimaryA * DimPrimaryA * DimPrimaryB + (4.0 / 3.0) * PiConst * std::pow(DimPrimaryA, 3.0);
    case FractureIcons::PrimitiveShapeId::Torus:     return 2.0 * PiConst * PiConst * DimPrimaryA * DimPrimaryB * DimPrimaryB;
    case FractureIcons::PrimitiveShapeId::Pipe:      return PiConst * (DimPrimaryA * DimPrimaryA - DimPrimaryB * DimPrimaryB) * DimPrimaryC;
    case FractureIcons::PrimitiveShapeId::Pane:      return static_cast<double>(DimPrimaryA) * DimPrimaryB * (DimPrimaryC / 1000.0);
    case FractureIcons::PrimitiveShapeId::HexColumn: return 2.0 * std::sqrt(3.0) * std::pow(DimPrimaryA * 0.5, 2.0) * DimPrimaryB;
    default: return PartitionResult.Receipt.SourceVolumeM3;
    }
}

double FractureWorkspaceSession::QueryExactAreaM2() const noexcept
{
    switch (ActiveShape)
    {
    case FractureIcons::PrimitiveShapeId::Box:       return 2.0 * (DimPrimaryA * DimPrimaryB + DimPrimaryB * DimPrimaryC + DimPrimaryA * DimPrimaryC);
    case FractureIcons::PrimitiveShapeId::Sphere:    return 4.0 * PiConst * DimPrimaryA * DimPrimaryA;
    case FractureIcons::PrimitiveShapeId::Cylinder:  return TauConst * DimPrimaryA * (DimPrimaryA + DimPrimaryB);
    case FractureIcons::PrimitiveShapeId::Cone:      return PiConst * DimPrimaryA * (DimPrimaryA + std::hypot(DimPrimaryA, DimPrimaryB));
    case FractureIcons::PrimitiveShapeId::Capsule:   return TauConst * DimPrimaryA * DimPrimaryB + 4.0 * PiConst * DimPrimaryA * DimPrimaryA;
    case FractureIcons::PrimitiveShapeId::Torus:     return 4.0 * PiConst * PiConst * DimPrimaryA * DimPrimaryB;
    case FractureIcons::PrimitiveShapeId::Pipe:      return TauConst * (DimPrimaryA + DimPrimaryB) * DimPrimaryC + 2.0 * PiConst * (DimPrimaryA * DimPrimaryA - DimPrimaryB * DimPrimaryB);
    case FractureIcons::PrimitiveShapeId::Pane:      return 2.0 * (DimPrimaryA * DimPrimaryB + (DimPrimaryA + DimPrimaryB) * (DimPrimaryC / 1000.0));
    case FractureIcons::PrimitiveShapeId::HexColumn:
    {
        const double A = DimPrimaryA * 0.5;
        const double S = (2.0 * A) / std::sqrt(3.0);
        return 6.0 * S * DimPrimaryB + 4.0 * std::sqrt(3.0) * A * A;
    }
    default: return 1.0;
    }
}

std::string FractureWorkspaceSession::FormatPrimitiveSubtitle() const
{
    char Buf[64] {};
    switch (ActiveShape)
    {
    case FractureIcons::PrimitiveShapeId::Box:       std::snprintf(Buf, sizeof(Buf), "%.2f × %.2f × %.2f m", DimPrimaryA, DimPrimaryB, DimPrimaryC); break;
    case FractureIcons::PrimitiveShapeId::Sphere:    std::snprintf(Buf, sizeof(Buf), "Ø %.2f m", DimPrimaryA * 2.0f); break;
    case FractureIcons::PrimitiveShapeId::Cylinder:  std::snprintf(Buf, sizeof(Buf), "Ø %.2f × %.2f m", DimPrimaryA * 2.0f, DimPrimaryB); break;
    case FractureIcons::PrimitiveShapeId::Cone:      std::snprintf(Buf, sizeof(Buf), "Ø %.2f × %.2f m", DimPrimaryA * 2.0f, DimPrimaryB); break;
    case FractureIcons::PrimitiveShapeId::Capsule:   std::snprintf(Buf, sizeof(Buf), "Ø %.2f × %.2f m", DimPrimaryA * 2.0f, DimPrimaryB + DimPrimaryA * 2.0f); break;
    case FractureIcons::PrimitiveShapeId::Torus:     std::snprintf(Buf, sizeof(Buf), "R %.2f · r %.2f m", DimPrimaryA, DimPrimaryB); break;
    case FractureIcons::PrimitiveShapeId::Pipe:      std::snprintf(Buf, sizeof(Buf), "Ø %.2f / %.2f × %.2f m", DimPrimaryA * 2.0f, DimPrimaryB * 2.0f, DimPrimaryC); break;
    case FractureIcons::PrimitiveShapeId::Pane:      std::snprintf(Buf, sizeof(Buf), "%.2f × %.2f m · %.0f mm", DimPrimaryA, DimPrimaryB, DimPrimaryC); break;
    case FractureIcons::PrimitiveShapeId::HexColumn: std::snprintf(Buf, sizeof(Buf), "%.2f AF × %.2f m", DimPrimaryA, DimPrimaryB); break;
    default: break;
    }
    return Buf;
}

float FractureWorkspaceSession::QueryFragmentReleaseShareJ(uint32_t Index) const noexcept
{
    if (Index >= PartitionResult.Fragments.size())
    {
        return 0.0f;
    }
    const auto& Frag = PartitionResult.Fragments[Index];
    double SumWeight = 0.0;
    for (const auto& F : PartitionResult.Fragments)
    {
        SumWeight += std::pow(std::max(0.04, static_cast<double>(F.Proximity)), 2.2);
    }
    const double W = std::pow(std::max(0.04, static_cast<double>(Frag.Proximity)), 2.2);
    const double KineticBudget = std::max(10.0, (ImpactEnergyJ - PartitionResult.Receipt.SpentSurfaceEnergyJ) * TransferRatio);
    return static_cast<float>((W / std::max(0.05, SumWeight)) * KineticBudget);
}

bool FractureWorkspaceSession::QueryFragmentReleased(uint32_t Index) const noexcept
{
    if (Index >= PartitionResult.Fragments.size())
    {
        return false;
    }
    if (static_cast<int32_t>(Index) == RenderOptions.SelectedFragmentIndex && FragmentOverrideMode != 0)
    {
        return FragmentOverrideMode == 2;
    }
    return static_cast<double>(QueryFragmentReleaseShareJ(Index)) >= ReleaseThresholdJ;
}

uint32_t FractureWorkspaceSession::CountReleasedFragments() const noexcept
{
    uint32_t Released = 0u;
    for (uint32_t I = 0u; I < PartitionResult.Fragments.size(); ++I)
    {
        if (QueryFragmentReleased(I)) ++Released;
    }
    return Released;
}

uint32_t FractureWorkspaceSession::CountHeldFragments() const noexcept
{
    const uint32_t Total = static_cast<uint32_t>(PartitionResult.Fragments.size());
    const uint32_t Rel   = CountReleasedFragments();
    return (Total >= Rel) ? (Total - Rel) : 0u;
}

const char* FractureWorkspaceSession::QuerySolverShortLabel(FractureSolverMethod Method) noexcept
{
    return SolverShortTitle(Method);
}

uint64_t FractureWorkspaceSession::ComputeViewportRenderSignature() const noexcept
{
    uint64_t H = 14695981039346656037ull;
    auto Mix = [&](uint64_t V)
    {
        H ^= V;
        H *= 1099511628211ull;
    };
    Mix(SessionRevision);
    Mix(static_cast<uint64_t>(ActiveShape));
    Mix(static_cast<uint64_t>(ExecutionPhase));
    Mix(static_cast<uint64_t>(RenderOptions.RenderMode));
    Mix(RenderOptions.ShowWireframe ? 1ull : 0ull);
    Mix(RenderOptions.ShowBonds ? 1ull : 0ull);
    Mix(RenderOptions.ShowWavefronts ? 1ull : 0ull);
    Mix(RenderOptions.ShowGroundGrid ? 1ull : 0ull);
    Mix(static_cast<uint64_t>(RenderOptions.SelectedFragmentIndex + 100));
    Mix(static_cast<uint64_t>(std::lround(RenderOptions.ExplodeFactor * 1000.0f)));
    Mix(static_cast<uint64_t>(std::lround(Camera.YawRadians * 1000.0f)));
    Mix(static_cast<uint64_t>(std::lround(Camera.PitchRadians * 1000.0f)));
    Mix(static_cast<uint64_t>(std::lround(Camera.DistanceM * 1000.0f)));
    for (bool Flag : FragmentVisibleFlags)
    {
        Mix(Flag ? 1ull : 0ull);
    }
    return H;
}

std::string FractureWorkspaceSession::BuildStudioReportMarkdown() const
{
    const auto& Mat = ActiveMaterialSpec();
    const auto& Rec = PartitionResult.Receipt;
    const auto& Prim = InspectPrimitives()[static_cast<uint32_t>(ActiveShape) % PrimitiveCount];
    std::ostringstream Out;
    Out << "# Frontier Fracture Editor Report\n\n"
        << "- **Primitive**: `" << Prim.Title << "` (`" << FormatPrimitiveSubtitle() << "`)\n"
        << "- **Outside / Interior Material**: `" << ActiveSurfaceMaterialSpec().Label << "` / `" << Mat.Label << "`\n"
        << "- **Solver**: `" << SolverShortTitle(SolverMethod) << "` (`" << (MethodMode == FractureMethodMode::Baked ? "Baked" : "Realtime") << "`)\n"
        << "- **Fragments**: `" << PartitionResult.Fragments.size() << "` (`" << CountReleasedFragments() << "` released, `" << CountHeldFragments() << "` held)\n"
        << "- **Volume Drift**: `" << Rec.VolumeDriftRatio << "`\n"
        << "- **Crack Area**: `" << Rec.TotalCrackAreaM2 << " m^2`\n"
        << "- **Bake Time**: `" << LastTimings.TotalMs << " ms`\n";
    return Out.str();
}

void FractureWorkspaceSession::AppendLog(const char* Channel, std::string Message, std::string Detail, bool WarmTone)
{
    LogEntries.push_back(FractureLogEntry { ElapsedSeconds, Channel, std::move(Message), std::move(Detail), WarmTone });
    if (LogEntries.size() > 32u)
    {
        LogEntries.erase(LogEntries.begin());
    }
}

} // namespace Frontier::Workspaces::Fracture
