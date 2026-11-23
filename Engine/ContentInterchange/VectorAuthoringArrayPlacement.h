/*============================================================================================================================================
                                                          VECTORAUTHORINGARRAYPLACEMENT.H
============================================================================================================================================*/
// 🧩 Workspace-independent array transforms and scale envelopes, reusable by vector vector and texture-paint layers.

#pragma once
#include <cstdint>
#include <vector>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     ARRAY SPECIFICATION AND PLACEMENTS
//------------------------------------------------------------------------------------------------------------------------

enum class VectorAuthoringArrayCategory : uint32_t
{
    Linear, Grid, Circular, Radial
};
enum class VectorAuthoringScaleProfile : uint32_t
{
    Uniform, Linear, Circular, Radial, Noise
};
struct VectorAuthoringArraySpecification
{
    VectorAuthoringArrayCategory Category       = VectorAuthoringArrayCategory::Linear;
    VectorAuthoringScaleProfile  ScaleProfile   = VectorAuthoringScaleProfile::Uniform;
    uint32_t             Count          = 4;
    uint32_t             Columns        = 2;
    uint32_t             Rows           = 2;
    uint32_t             Rings          = 1;
    uint32_t             Seed           = 1;
    float                StartScale     = 100;
    float                EndScale       = 100;
    float                StepX          = 64;
    float                StepY          = 0;
    float                CentreX        = 256;
    float                CentreY        = 256;
    float                Radius         = 100;
    float                RadiusStep     = 64;
    float                StartAngle     = 0;
    float                Sweep          = 360;
    bool                 RotateCopies   = false;
};
struct VectorAuthoringArrayPlacement
{
    float X     = 0;
    float Y     = 0;
    float Angle = 0;
    float Phase = 0;
    float Scale = 1;
};

std::vector<VectorAuthoringArrayPlacement> ConstructVectorAuthoringArrayPlacements(const VectorAuthoringArraySpecification &Specification, float OriginX, float OriginY);
}
