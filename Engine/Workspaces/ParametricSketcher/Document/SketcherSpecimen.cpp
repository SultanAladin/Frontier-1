/*====================================================================================================================================
                                                         SOLIDARCSPECIMEN.CPP
====================================================================================================================================*/
// 🧩 The bracket specimen: one modelling session (tiles, boolean, picks, fillet, chamfer) shared by validation and captures

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherSpecimen.h"

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherScene.h"

#include <cmath>

namespace Frontier::Workspaces::Sketcher
{

using namespace Frontier::GeometrySpace::Parametric;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void Note(std::vector<std::string>* Notes, const SketcherCommandOutcome& Outcome)
{
    if (Notes && !Outcome.AcceptedCondition) Notes->push_back(Outcome.Label + ": " + Outcome.Refusal);
}

// Selects whatever the ray from From towards Towards hits first (edge within 0.02, else face).
void PickTowards(SketcherDocument& Document, std::array<double, 3> From, std::array<double, 3> Towards)
{
    std::array<double, 3> Direction = { Towards[0] - From[0], Towards[1] - From[1], Towards[2] - From[2] };
    const double Length = std::sqrt(Direction[0] * Direction[0] + Direction[1] * Direction[1] + Direction[2] * Direction[2]);
    for (double& Component : Direction) Component /= Length;
    const SketcherScene               Scene = BuildSketcherScene(Document);
    const std::optional<SketcherPick> Hit   = PickSketcherScene(Scene, { From, Direction }, 0.02);
    if (Hit) SelectSketcherName(Document, SketcherIdentityKind(Hit->Identity), NameSketcherIdentity(Scene, Hit->Identity), false);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketcherDocument BuildSketcherSpecimen(std::vector<std::string>* Notes)
{
    SketcherDocument Document = CreateSketcherDocument("Bracket");
    SketcherConstructRequest Request;
    Request.Kind = SketcherConstructKind::Block; Request.Width = 4.0; Request.Depth = 2.5; Request.Height = 1.0;
    Note(Notes, ConstructSketcherPrimitive(Document, Request));
    Request = {};
    Request.Kind = SketcherConstructKind::Cylinder; Request.Position = { 1.0, 1.25, 1.0 }; Request.Radius = 0.5; Request.Height = 0.75;
    Request.Combination = FeatureCombination::Union;
    Note(Notes, ConstructSketcherPrimitive(Document, Request));
    Request = {};
    Request.Kind = SketcherConstructKind::Cylinder; Request.Position = { 3.0, 1.25, -0.5 }; Request.Radius = 0.3; Request.Height = 2.0;
    Request.Combination = FeatureCombination::Difference;
    Note(Notes, ConstructSketcherPrimitive(Document, Request));
    PickTowards(Document, { 5.0, 0.6, 2.0 }, { 4.0, 0.6, 1.0 });
    Note(Notes, BlendSketcherSelection(Document, BlendProfile::Fillet, 0.25));
    PickTowards(Document, { -1.0, 0.6, 2.0 }, { 0.0, 0.6, 1.0 });
    Note(Notes, BlendSketcherSelection(Document, BlendProfile::Chamfer, 0.2));
    ClearSketcherSelection(Document);
    return Document;
}

} // namespace Frontier::Workspaces::Sketcher
