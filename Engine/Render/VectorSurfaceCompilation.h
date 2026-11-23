/*============================================================================================================================================
                                                       VECTORSURFACECOMPILATION.H
============================================================================================================================================*/
// 🧩 Compile ordered vector layer channel contributions into a bounded GPU Float32 surface program.

#pragma once
#include "Engine/ContentInterchange/VectorAuthoringDocument.h"

namespace Frontier
{
struct VectorSurfaceContribution
{
    uint32_t Root = 0;
    uint32_t Token = 0;
    VectorAuthoringSurfaceChannels Channels = ConstructVectorAuthoringSurfaceChannels();
    float SurfaceRelief = 1.f;
    bool operator==(const VectorSurfaceContribution &) const = default;
};
struct VectorSurfaceProgram
{
    VectorCoverageProgram Coverage;
    std::vector<VectorSurfaceContribution> Contributions;
    VectorAuthoringSurfaceChannel Channel = VectorAuthoringSurfaceChannel::Colour;
    bool operator==(const VectorSurfaceProgram &) const = default;
};
VectorSurfaceProgram CompileVectorSurface(
    const VectorAuthoringDocument &Document,
    VectorAuthoringSurfaceChannel Channel);
void ValidateVectorSurface(const VectorSurfaceProgram &Program);
}
