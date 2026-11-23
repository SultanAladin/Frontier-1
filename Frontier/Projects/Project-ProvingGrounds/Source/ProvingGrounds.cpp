/*==========================================================================================================================================
                                                 GEOMETRY IS BUILT ONCE AND SPUN BY MATRIX
                                     ------------------------------------------------------------------
                                             The vertex data never changes after construction.
==========================================================================================================================================*/

// 🧩 Geometry is built once and spun by matrix: the vertex data never changes after construction.

#include "ProvingGrounds.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ProvingGroundsGeometry BuildProvingGroundsGeometry()
{
    ProvingGroundsGeometry Geometry;

    // Six faces, four corners each (flat normals), wound counter-clockwise seen from outside.
    struct Face
    {
        Vec3 Normal;
        Vec3 Colour;
        Vec3 Corners[4];
    };
    const Face Faces[] = {
        {{0, 0, 1},  {0.18f, 0.85f, 0.62f}, {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}},       // +Z mint
        {{0, 0, -1}, {0.20f, 0.45f, 0.95f}, {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}}},  // -Z blue
        {{1, 0, 0},  {0.95f, 0.45f, 0.25f}, {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}}},       // +X ember
        {{-1, 0, 0}, {0.65f, 0.40f, 0.95f}, {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}}},  // -X violet
        {{0, 1, 0},  {0.98f, 0.80f, 0.30f}, {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}}},       // +Y amber
        {{0, -1, 0}, {0.30f, 0.32f, 0.36f}, {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}}},  // -Y slate
    };
    for (const Face& Face : Faces)
    {
        const uint32_t Base = static_cast<uint32_t>(Geometry.Vertices.size());
        for (const Vec3& Corner : Face.Corners)
        {
            Geometry.Vertices.push_back(ProvingGroundsVertex{Corner * 0.5f + Vec3{0, 0.55f, 0}, Face.Normal, Face.Colour});
        }
        Geometry.Indices.insert(Geometry.Indices.end(), {Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
    }

    // Ground plane at y = 0 (the cube floats its base just above it at rest).
    {
        const uint32_t Base   = static_cast<uint32_t>(Geometry.Vertices.size());
        const Vec3     Normal = {0, 1, 0};
        const Vec3     Colour = {0.16f, 0.17f, 0.19f};
        Geometry.Vertices.push_back(ProvingGroundsVertex{{-4, 0, 4}, Normal, Colour});
        Geometry.Vertices.push_back(ProvingGroundsVertex{{4, 0, 4}, Normal, Colour});
        Geometry.Vertices.push_back(ProvingGroundsVertex{{4, 0, -4}, Normal, Colour});
        Geometry.Vertices.push_back(ProvingGroundsVertex{{-4, 0, -4}, Normal, Colour});
        Geometry.Indices.insert(Geometry.Indices.end(), {Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
    }

    return Geometry;
}

void ProvingGroundsSimulation::Advance(double Δτ) noexcept
{
    Angle            += AngularRate * Δτ;
    SimulatedSeconds += Δτ;
    FrameIndex       += 1;
}

ProvingGroundsSnapshot ProvingGroundsSimulation::TakeSnapshot(uint32_t ViewportWidth, uint32_t ViewportHeight) const noexcept
{
    const float Aspect = ViewportHeight > 0
        ? static_cast<float>(ViewportWidth) / static_cast<float>(ViewportHeight)
        : 1.0f;

    ProvingGroundsSnapshot Snapshot;
    Snapshot.ModelToWorld   = RotationMatrix(Vec3{0, 1, 0}, static_cast<float>(Angle));
    Snapshot.ViewProjection = PerspectiveMatrix(0.9f, Aspect, 0.1f, 100.0f) *
                              LookAtMatrix(Vec3{1.9f, 1.35f, 2.5f}, Vec3{0, 0.45f, 0}, Vec3{0, 1, 0});
    Snapshot.FrameIndex       = FrameIndex;
    Snapshot.SimulatedSeconds = SimulatedSeconds;
    return Snapshot;
}

} // namespace Frontier
