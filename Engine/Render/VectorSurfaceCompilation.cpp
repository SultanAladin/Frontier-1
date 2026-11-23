/*============================================================================================================================================
                                                       VECTORSURFACECOMPILATION.CPP
============================================================================================================================================*/
// 🧩 Preserve ordered masks and per-layer channel strengths while deriving independent scalar and colour outputs.

#include "VectorSurfaceCompilation.h"
#include <unordered_set>

namespace Frontier
{
void ValidateVectorSurface(const VectorSurfaceProgram &Program)
{
    ValidateVectorCoverage(Program.Coverage);
    if (uint32_t(Program.Channel) > 6 || Program.Contributions.size() > VectorCoverageExtensionCapacity)
        throw std::runtime_error("Vector surface channel or contribution population is invalid.");
    std::unordered_set<uint32_t> Tokens;
    for (const auto &Contribution : Program.Contributions)
    {
        if (Contribution.Root >= Program.Coverage.Instructions.size() || !Contribution.Token ||
            !Tokens.insert(Contribution.Token).second)
            throw std::runtime_error("Vector surface root or layer token is invalid.");
        ValidateVectorAuthoringSurfaceChannels(Contribution.Channels);
        if (!std::isfinite(Contribution.SurfaceRelief) || Contribution.SurfaceRelief < -1.f || Contribution.SurfaceRelief > 1.f)
            throw std::runtime_error("Compiled vector relief exceeds its finite signed interval.");
    }
}

VectorSurfaceProgram CompileVectorSurface(const VectorAuthoringDocument &Document, VectorAuthoringSurfaceChannel Channel)
{
    VectorSurfaceProgram Program;
    Program.Channel = Channel;
    Program.Coverage = {Document.Instructions, Document.Segments, 0};
    (void)ConstructVectorAuthoringCoverage(Document);
    std::optional<uint32_t> Output;
    for (const auto &Layer : Document.Layers)
    {
        if (!QueryVectorAuthoringVisibility(Document, Layer)) continue;
        uint32_t Root = Layer.Root;
        if (Layer.Mask && Layer.MaskEnabled)
        {
            const uint32_t Mask = uint32_t(Program.Coverage.Instructions.size());
            Program.Coverage.Instructions.push_back(*Layer.Mask);
            VectorCoverageInstruction Operation;
            Operation.Topology = {Layer.MaskLuminance ? 25u : 24u, Root, Mask, Layer.MaskInverted ? 1u : 0u};
            Root = uint32_t(Program.Coverage.Instructions.size());
            Program.Coverage.Instructions.push_back(Operation);
        }
        Program.Contributions.push_back({Root, Layer.Token, Layer.SurfaceChannels, Layer.SurfaceRelief});
        if (Output)
        {
            VectorCoverageInstruction Operation;
            Operation.Topology = {28, *Output, Root, 0};
            Output = uint32_t(Program.Coverage.Instructions.size());
            Program.Coverage.Instructions.push_back(Operation);
        }
        else Output = Root;
    }
    if (!Output)
    {
        VectorCoverageInstruction Transparent;
        Transparent.Transform[2] = 0;
        Program.Coverage = {{Transparent}, {}, 0};
    }
    else Program.Coverage.Root = *Output;
    ValidateVectorSurface(Program);
    return Program;
}
}
