/*============================================================================================================================================
                                                       SKETCHERKERNELVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation exhibit for Phase 1 Parametric Sketcher B-Rep & 2D Sketch Kernel (`Engine/GeometrySpace/Parametric/`)

#include "Engine/GeometrySpace/Parametric/Blend/EdgeBlend.h"
#include "Engine/GeometrySpace/Parametric/Boolean/GeneralFuse.h"
#include "Engine/GeometrySpace/Parametric/Construction/PrimitiveConstruction.h"
#include "Engine/GeometrySpace/Parametric/Exchange/StepExchange.h"
#include "Engine/GeometrySpace/Parametric/Exchange/StlExchange.h"
#include "Engine/GeometrySpace/Parametric/Feature/FeatureRepository.h"
#include "Engine/GeometrySpace/Parametric/Feature/FeatureTree.h"
#include "Engine/GeometrySpace/Parametric/Measurement/MassProperties.h"
#include "Engine/Interchange/GeometryInterchange.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchCorner.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSolver.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

using namespace Frontier::GeometrySpace;
using namespace Frontier::GeometrySpace::Parametric;

bool VerifyCheck(bool Condition, const char* Message) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[SketcherKernelValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    uint32_t Passed = 0u;

    // 1. 2D Sketch Constraint Solving (Constrained rectangle)
    {
        SketchModel Sketch {};
        const int P0 = AppendSketchPoint(Sketch, { 0.0, 0.0 });
        const int P1 = AppendSketchPoint(Sketch, { 3.2, 0.2 });
        const int P2 = AppendSketchPoint(Sketch, { 3.1, 2.3 });
        const int P3 = AppendSketchPoint(Sketch, { 0.1, 2.2 });
        const int L0 = AppendSketchLine(Sketch, P0, P1, 1);
        const int L1 = AppendSketchLine(Sketch, P1, P2, 2);
        const int L2 = AppendSketchLine(Sketch, P2, P3, 3);
        const int L3 = AppendSketchLine(Sketch, P3, P0, 4);

        (void)AppendSketchConstraint(Sketch, SketchConstraintCategory::Fixed, P0, -1, -1, 0.0, {}, 0.0);
        (void)AppendSketchConstraint(Sketch, SketchConstraintCategory::Horizontal, L0);
        (void)AppendSketchConstraint(Sketch, SketchConstraintCategory::Vertical, L1);
        (void)AppendSketchConstraint(Sketch, SketchConstraintCategory::Horizontal, L2);
        (void)AppendSketchConstraint(Sketch, SketchConstraintCategory::Vertical, L3);
        (void)AppendSketchConstraint(Sketch, SketchConstraintCategory::Length, L0, -1, -1, 4.0);
        (void)AppendSketchConstraint(Sketch, SketchConstraintCategory::Length, L1, -1, -1, 2.5);

        const SketchSolveOutcome Solve = SolveSketch(Sketch);
        if (!VerifyCheck(Solve.DegreesOfFreedom == 0 && Solve.MaxResidual < 1.0e-8, "2D sketch solved to 0 DOF"))
        {
            return 1;
        }
        if (!VerifyCheck(std::abs(Sketch.Points[P2].Position.X - 4.0) < 1.0e-6 &&
                         std::abs(Sketch.Points[P2].Position.Y - 2.5) < 1.0e-6,
                         "Solved rectangle dimensions match 4.0 x 2.5"))
        {
            return 1;
        }
        ++Passed;
    }

    // 2. Solid Primitives, General Fuse Difference & MassProperties
    {
        ModelPartition Partition {};
        const auto BlockOut = ConstructBlock(Partition, { -2.0, -2.0, 0.0 }, { 4.0, 4.0, 2.0 });
        const auto CylOut = ConstructCylinder(
            Partition,
            OrthonormalBasis { { 0.0, 0.0, -0.5 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } },
            1.0,
            3.0);
        if (!VerifyCheck(BlockOut.SucceededCondition() && CylOut.SucceededCondition(), "ConstructBlock & ConstructCylinder"))
        {
            return 1;
        }

        const BooleanOutcome Diff = ComputeBoolean(Partition, BlockOut.Value, CylOut.Value, BooleanOperation::Difference);
        if (!VerifyCheck(Diff.Refusal.empty(), "GeneralFuse Boolean Difference succeeded"))
        {
            return 1;
        }

        const BodyMassProperties Mass = MeasureBodyMass(Partition, Diff.Body);
        const double ExpectedVol = (4.0 * 4.0 * 2.0) - (3.14159265358979323846 * 1.0 * 1.0 * 2.0);
        if (!VerifyCheck(Mass.Refusal.empty() && std::abs(Mass.Volume - ExpectedVol) < 1.0e-3,
                         "Boolean Difference volume matches analytical box - cylinder"))
        {
            return 1;
        }

        // Convert to Frontier::GeometrySpace::ConstructedSampleAsset via Frontier Interchange
        ConstructedSampleAsset InterchangeAsset {};
        std::string InterchangeDiag {};
        if (!VerifyCheck(Frontier::Interchange::GeometryInterchange::ImportParametricBody(
                             Partition, Diff.Body, TessellationSettings {}, "ParametricCutBox", InterchangeAsset, InterchangeDiag),
                         "GeometryInterchange::ImportParametricBody produces valid ConstructedSampleAsset"))
        {
            std::fprintf(stderr, "Interchange diagnostic: %s\n", InterchangeDiag.c_str());
            return 1;
        }
        if (!VerifyCheck(!InterchangeAsset.Network.Faces.empty() && !InterchangeAsset.Network.Vertices.empty(), "PolygonNetwork non-empty"))
        {
            return 1;
        }
        const std::string ObjText = Frontier::Interchange::GeometryInterchange::ExportWavefrontObjText(InterchangeAsset.Network, &InterchangeAsset.Layers);
        if (!VerifyCheck(ObjText.find("v ") != std::string::npos && ObjText.find("f ") != std::string::npos, "GeometryInterchange Wavefront OBJ export non-empty"))
        {
            return 1;
        }
        ++Passed;
    }

    // 3. Rolling-Ball Edge Blend (Fillet) + STEP Roundtrip
    {
        ModelPartition Partition {};
        const auto BlockOut = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 3.0, 2.0, 1.5 });
        if (!VerifyCheck(BlockOut.SucceededCondition(), "ConstructBlock for EdgeBlend"))
        {
            return 1;
        }
        const std::vector<EdgeToken> Edges = Partition.CollectInventory(BlockOut.Value).Edges;
        if (!VerifyCheck(Edges.size() == 12u, "Block has 12 topological edges"))
        {
            return 1;
        }
        BlendSettings BlendCfg {};
        BlendCfg.Profile = BlendProfile::Fillet;
        BlendCfg.Size    = 0.25;
        const BlendOutcome Filleted = BlendEdges(Partition, BlockOut.Value, { Edges[0] }, BlendCfg);
        if (!VerifyCheck(Filleted.Refusal.empty(), "Rolling-ball EdgeBlend succeeded"))
        {
            std::fprintf(stderr, "Blend refusal: %s\n", Filleted.Refusal.c_str());
            return 1;
        }

        StepRequest StepReq {};
        StepReq.Name = "ParametricFilletBlock";
        const StepExport Export = WriteStep(Partition, { Filleted.Body }, StepReq);
        if (!VerifyCheck(Export.Refusal.empty() && Export.Solids == 1 && !Export.Bytes.empty(), "STEP export valid"))
        {
            return 1;
        }
        ModelPartition ImportedPart {};
        const StepImport Import = ReadStep(ImportedPart, Export.Bytes, StepImportRequest {});
        if (!VerifyCheck(Import.Refusal.empty() && !Import.Bodies.empty(), "STEP roundtrip import valid"))
        {
            return 1;
        }
        ++Passed;
    }

    // 4. Parametric FeatureTree Specimen Replay + Parameter Sweep
    {
        const std::vector<std::string> Specimens = EnumerateFeatureSpecimens();
        if (!VerifyCheck(!Specimens.empty(), "EnumerateFeatureSpecimens non-empty"))
        {
            return 1;
        }
        FeatureSpecimen Spec = BuildFeatureSpecimen(Specimens.front());
        ModelPartition Partition {};
        const RegenerationOutcome FirstRun = RegenerateFeatureTree(Partition, Spec.Tree, true);
        if (!VerifyCheck(FirstRun.Refused == 0 && FirstRun.Bodies.size() >= 1u, "FeatureSpecimen initial regeneration succeeded"))
        {
            return 1;
        }
        if (Spec.Sweep.size() >= 2u)
        {
            (void)AssignFeatureParameters(Spec.Tree, Spec.Sweep[1]);
            ModelPartition SweepPart {};
            const RegenerationOutcome SweepRun = RegenerateFeatureTree(SweepPart, Spec.Tree, false);
            if (!VerifyCheck(SweepRun.Refused == 0 && !SweepRun.Bodies.empty(), "FeatureSpecimen parameter sweep regeneration succeeded"))
            {
                return 1;
            }
        }
        ++Passed;
    }

    std::printf("[SketcherKernelValidation] Passed=%u Failed=0\n", Passed);
    return 0;
}
