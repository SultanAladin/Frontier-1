/*====================================================================================================================================
                                                   FACEOPERATIONCATALOGUE.CPP
====================================================================================================================================*/
// 🧩 Face-operation specimens with hand-derived closed forms

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Local/FaceOperationCatalogue.h"

#include "Engine/GeometrySpace/Parametric/Boolean/GeneralFuse.h"
#include "Engine/GeometrySpace/Parametric/Construction/PrimitiveConstruction.h"

#include <functional>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRIVATE HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double π = std::numbers::pi;

    // 📝 Section-E dimensions (FeatureCorpus.arc, translated to the origin).
    constexpr double BlockX = 2.0, BlockY = 1.5, BlockZ = 1.0;                                // [len] - Block extents
    constexpr double SlabX  = 3.0, SlabY  = 2.0, SlabZ  = 0.6;                                // [len] - Slab extents
    constexpr double BoreRadius = 0.45;                                                       // [len] - Bore through the slab centre
    constexpr double CanRadius  = 0.6, CanHeight = 1.0;                                       // [len] - Can

    using FaceTest = std::function<bool(const SurfaceGeometry&, Vector3)>;

    [[nodiscard]] std::vector<FaceToken> SelectFaces(const ModelPartition& Partition, BodyToken Body, const FaceTest& Test)
    {
        std::vector<FaceToken> Selected;
        for (FaceToken Face : Partition.CollectInventory(Body).Faces)
        {
            const Vector3 Centre = MeasureFaceBoundaryCentre(Partition, Face);
            if (Test(Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface), EvaluateOutwardNormal(Partition, Face, Centre))) Selected.push_back(Face);
        }
        return Selected;
    }

    [[nodiscard]] FaceTest Facing(Vector3 Direction)
    {
        return [Direction](const SurfaceGeometry& Surface, Vector3 Normal) { return std::holds_alternative<PlaneSurface>(Surface) && Dot(Normal, Direction) > 1.0 - 1.0e-9; };
    }

    [[nodiscard]] FaceTest Curved()
    {
        return [](const SurfaceGeometry& Surface, Vector3) { return !std::holds_alternative<PlaneSurface>(Surface); };
    }

    [[nodiscard]] FaceTest Sides()
    {
        return [](const SurfaceGeometry& Surface, Vector3 Normal) { return std::holds_alternative<PlaneSurface>(Surface) && std::fabs(Normal.Z) < 1.0e-9; };
    }

    // Corpus "+x" pick: the planar +x face, or the curved side of the Can (the face holding the +x extreme point).
    [[nodiscard]] FaceTest PlusX(int Which) { return Which == 2 ? Curved() : Facing({ 1.0, 0.0, 0.0 }); }

    [[nodiscard]] FaceOperationSettings Operation(FaceOperationKind Kind, std::vector<FaceToken> Faces = {}) { FaceOperationSettings Settings; Settings.Kind = Kind; Settings.Faces = std::move(Faces); return Settings; }

    const char* const BodyNames[3] = { "Block", "HoleSlab", "Can" };

    // 📝 Areas and volumes of the three sources, s = length scale.
    struct CorpusMeasures
    {
        double TopArea = 0.0;                                                                 // [len²] - +z face area
        double Volume  = 0.0;                                                                 // [len³] - Source volume
        int    Genus   = 0;                                                                   // [-]    - Source genus
        std::array<int, 3> Counts = {};                                                       // [-]    - F, E, V
    };

    [[nodiscard]] CorpusMeasures MeasureCorpus(int Which, double Scale)
    {
        const double Area = Scale * Scale, Cube = Area * Scale;
        if (Which == 0) return { BlockX * BlockY * Area, BlockX * BlockY * BlockZ * Cube, 0, { 6, 12, 8 } };
        if (Which == 1) { const double Top = SlabX * SlabY - π * BoreRadius * BoreRadius; return { Top * Area, Top * SlabZ * Cube, 1, { 7, 15, 10 } }; }
        return { π * CanRadius * CanRadius * Area, π * CanRadius * CanRadius * CanHeight * Cube, 0, { 3, 3, 2 } };
    }

    // ∫₀ʰ π (r + s z)² dz — a drafted / tapered round wall.
    [[nodiscard]] double ConeVolume(double Radius, double Slope, double Height) { return π * (Radius * Radius * Height + Radius * Slope * Height * Height + Slope * Slope * Height * Height * Height / 3.0); }

    // h/3 (A₀ + A₁ + √(A₀A₁)) — frustum between similar sections.
    [[nodiscard]] double FrustumVolume(double Lower, double Upper, double Height) { return Height / 3.0 * (Lower + Upper + std::sqrt(Lower * Upper)); }

    struct CorpusRow
    {
        const char* Verb;                                                                     // Specimen prefix
        const char* Command;                                                                  // Corpus verb
        const char* Legacy[3];                                                                // Old FaceEditSolver result per body
    };

    // 📝 FeatureSuitability.md, section E (17 refusals in the old kernel).
    const CorpusRow CorpusRows[] =
    {
        { "Push",         "push",         { "ran", "ran", "ran" } },
        { "ExtrudeFaces", "extrudefaces", { "ran", "ran", "ran" } },
        { "Tweak",        "tweak",        { "ran", "refused", "ran" } },
        { "OffsetFace",   "offsetface",   { "ran", "refused", "refused" } },
        { "Scale",        "scale",        { "ran", "refused", "refused" } },
        { "Rotate",       "rotate",       { "ran (2.994292)", "refused", "refused" } },
        { "Hollow",       "shell",        { "ran", "refused", "refused" } },
        { "Draft",        "draft",        { "ran", "refused", "refused" } },
        { "ExtendFace",   "extendface",   { "ran", "refused", "refused" } },
        { "TrimFace",     "trimface",     { "ran", "refused", "refused" } },
        { "DeleteFace",   "deleteface",   { "ran (sheet)", "refused", "refused" } },
        { "Heal",         "heal",         { "ran", "ran", "ran" } },
    };

    [[nodiscard]] FaceOperationSpecimen ConstructCorpusSpecimen(ModelPartition& Partition, const CorpusRow& Row, int Which, double Scale)
    {
        FaceOperationSpecimen Specimen;
        const std::string     Verb = Row.Verb;
        const CorpusMeasures  Measure = MeasureCorpus(Which, Scale);
        const double          Tan5 = std::tan(5.0 * π / 180.0);
        Specimen.Name         = Verb + BodyNames[Which];
        Specimen.Family       = "CorpusE";
        Specimen.LegacyStatus = Row.Legacy[Which];
        Specimen.Body         = ConstructCorpusBody(Partition, Which, Scale);
        Specimen.ExpectedGenus = Measure.Genus;
        const std::vector<FaceToken> Top = SelectFaces(Partition, Specimen.Body, Facing({ 0.0, 0.0, 1.0 }));
        const double Lift = (Which == 1 ? 0.2 : 0.5) * Scale;
        const std::string Pick = std::string(" ") + BodyNames[Which] + " --face=+z";
        if (Verb == "Push" || Verb == "ExtrudeFaces" || Verb == "Tweak" || Verb == "OffsetFace")
        {
            const FaceOperationKind Kind = Verb == "Push" ? FaceOperationKind::Push : Verb == "ExtrudeFaces" ? FaceOperationKind::Extrude : Verb == "Tweak" ? FaceOperationKind::Tweak : FaceOperationKind::Offset;
            FaceOperationSettings Settings = Operation(Kind, Top);
            Settings.Distance = Lift;
            Settings.Vector   = { 0.0, 0.0, Lift };
            Specimen.Steps          = { Settings };
            Specimen.ExpectedVolume = Measure.Volume + Measure.TopArea * Lift;
            Specimen.Description    = "+z face out by " + std::to_string(Lift / Scale).substr(0, 3) + ": V + A·d";
            Specimen.Corpus         = std::string(Row.Command) + Pick + " " + std::to_string(Lift / Scale).substr(0, 3);
            if (Kind != FaceOperationKind::Extrude) Specimen.ExpectedCounts = Measure.Counts;
        }
        else if (Verb == "Scale")
        {
            FaceOperationSettings Settings = Operation(FaceOperationKind::Scale, Top);
            Settings.Factor = 0.5;
            Specimen.Steps  = { Settings };
            const double Cube = Scale * Scale * Scale;
            if (Which == 0) Specimen.ExpectedVolume = FrustumVolume(BlockX * BlockY, BlockX * BlockY * 0.25, BlockZ) * Cube;
            if (Which == 1) Specimen.ExpectedVolume = (FrustumVolume(SlabX * SlabY, SlabX * SlabY * 0.25, SlabZ) - ConeVolume(BoreRadius, -0.5 * BoreRadius / SlabZ, SlabZ)) * Cube;
            if (Which == 2) Specimen.ExpectedVolume = ConeVolume(CanRadius, -0.5 * CanRadius / CanHeight, CanHeight) * Cube;
            Specimen.ExpectedCounts = Measure.Counts;
            Specimen.Description    = "+z face × 0.5 about its centre, neighbours re-hinged (plane → plane, cylinder → cone): frustum";
            Specimen.Corpus         = std::string(Row.Command) + Pick + " 0.5";
        }
        else if (Verb == "Rotate")
        {
            FaceOperationSettings Settings = Operation(FaceOperationKind::Rotate, Top);
            Settings.Angle = 5.0 * π / 180.0;
            Settings.Axis  = { 1.0, 0.0, 0.0 };
            Specimen.Steps          = { Settings };
            Specimen.ExpectedVolume = Measure.Volume;                                       // Symmetric about the hinge: gain = loss
            Specimen.ExpectedCounts = Measure.Counts;
            Specimen.Description    = "+z carrier turned 5° about x through the face centre; neighbours kept, edges re-intersected (ellipses on round walls)";
            Specimen.Corpus         = std::string(Row.Command) + Pick + " 5 --axis=(1,0,0)";
        }
        else if (Verb == "Hollow")
        {
            FaceOperationSettings Settings = Operation(FaceOperationKind::Hollow, Top);
            Settings.Distance = 0.1 * Scale;
            Specimen.Steps    = { Settings };
            const double Wall = 0.1, Cube = Scale * Scale * Scale;
            if (Which == 0) Specimen.ExpectedVolume = (BlockX * BlockY * BlockZ - (BlockX - 2 * Wall) * (BlockY - 2 * Wall) * (BlockZ - Wall)) * Cube;
            if (Which == 1) Specimen.ExpectedVolume = Measure.Volume - ((SlabX - 2 * Wall) * (SlabY - 2 * Wall) - π * (BoreRadius + Wall) * (BoreRadius + Wall)) * (SlabZ - Wall) * Cube;
            if (Which == 2) Specimen.ExpectedVolume = Measure.Volume - π * (CanRadius - Wall) * (CanRadius - Wall) * (CanHeight - Wall) * Cube;
            Specimen.Description = "Wall 0.1, +z open: cavity by face change, then General Fuse difference";
            Specimen.Corpus      = std::string(Row.Command) + Pick + " 0.1";
        }
        else if (Verb == "Draft")
        {
            FaceOperationSettings Settings = Operation(FaceOperationKind::Draft, SelectFaces(Partition, Specimen.Body, PlusX(Which)));
            Settings.Angle = 5.0 * π / 180.0;
            Specimen.Steps = { Settings };
            const double Cube = Scale * Scale * Scale;
            if (Which == 0) Specimen.ExpectedVolume = Measure.Volume + 0.5 * BlockZ * BlockZ * Tan5 * BlockY * Cube;
            if (Which == 1) Specimen.ExpectedVolume = Measure.Volume + 0.5 * SlabZ * SlabZ * Tan5 * SlabY * Cube;
            if (Which == 2) Specimen.ExpectedVolume = ConeVolume(CanRadius, Tan5, CanHeight) * Cube;
            Specimen.ExpectedCounts = Measure.Counts;
            Specimen.Description    = Which == 2 ? "Round side 5°, pull +z, neutral at the base: cylinder → cone" : "+x face 5°, pull +z, neutral at the base: hinge wedge ½h²·tan a·depth";
            Specimen.Corpus         = std::string(Row.Command) + " " + BodyNames[Which] + " --face=+x 5";
        }
        else if (Verb == "ExtendFace" || Verb == "TrimFace")
        {
            const bool   Grow = Verb == "ExtendFace";
            const double Step = Grow ? 0.2 : -0.2;
            FaceOperationSettings Settings = Operation(Grow ? FaceOperationKind::Extend : FaceOperationKind::Trim, Top);
            Settings.Distance = 0.2 * Scale;
            Specimen.Steps    = { Settings };
            const double Cube = Scale * Scale * Scale;
            if (Which == 0) Specimen.ExpectedVolume = (BlockX + 2 * Step) * (BlockY + 2 * Step) * BlockZ * Cube;
            if (Which == 1) Specimen.ExpectedVolume = ((SlabX + 2 * Step) * (SlabY + 2 * Step) - π * (BoreRadius - Step) * (BoreRadius - Step)) * SlabZ * Cube;
            if (Which == 2) Specimen.ExpectedVolume = π * (CanRadius + Step) * (CanRadius + Step) * CanHeight * Cube;
            Specimen.ExpectedCounts = Measure.Counts;
            Specimen.Description    = std::string("Neighbours of +z offset by ") + (Grow ? "+0.2 (face grows)" : "−0.2 (face shrinks)");
            Specimen.Corpus         = std::string(Row.Command) + Pick + " 0.2";
        }
        else if (Verb == "DeleteFace")
        {
            Specimen.Steps            = { Operation(FaceOperationKind::Delete, Top) };
            Specimen.ExpectedCategory = BodyCategory::Sheet;
            Specimen.ExpectedGenus    = 0;
            Specimen.ExpectedCounts   = { Measure.Counts[0] - 1, Measure.Counts[1], Measure.Counts[2] };
            Specimen.Description      = "+z removed: no hole loop to heal, so an open sheet (laminar rim)";
            Specimen.Corpus           = std::string(Row.Command) + Pick;
        }
        else
        {
            Specimen.Steps          = { Operation(FaceOperationKind::Heal) };
            Specimen.ExpectedVolume = Measure.Volume;
            Specimen.ExpectedCounts = Measure.Counts;
            Specimen.Description    = "Nothing on one carrier to merge: equal copy";
            Specimen.Corpus         = std::string(Row.Command) + " " + BodyNames[Which];
        }
        return Specimen;
    }

    using SpecimenBuilder = std::function<FaceOperationSpecimen(ModelPartition&, double)>;

    // 📝 Chains (verbs composed; the Reference is an independent construction that must agree), extras and refusals.
    [[nodiscard]] const std::vector<std::pair<std::string, SpecimenBuilder>>& FurtherSpecimens()
    {
        static const std::vector<std::pair<std::string, SpecimenBuilder>> Specimens = []
        {
            std::vector<std::pair<std::string, SpecimenBuilder>> List;
            for (int Which = 0; Which < 3; ++Which)
            {
                List.push_back({ std::string("HealExtrude") + BodyNames[Which], [Which](ModelPartition& Partition, double Scale)
                {
                    FaceOperationSpecimen Specimen;
                    Specimen.Name = std::string("HealExtrude") + BodyNames[Which]; Specimen.Family = "Chain";
                    Specimen.Description = "Heal(ExtrudeFaces(+z)) ≡ Push(+z): coplanar ring merged, edges joined";
                    Specimen.Body = ConstructCorpusBody(Partition, Which, Scale);
                    const std::vector<FaceToken> Top = SelectFaces(Partition, Specimen.Body, Facing({ 0.0, 0.0, 1.0 }));
                    FaceOperationSettings Extrude = Operation(FaceOperationKind::Extrude, Top), Push = Operation(FaceOperationKind::Push, Top);
                    Extrude.Distance = Push.Distance = 0.5 * Scale;
                    Specimen.Steps = { Extrude, Operation(FaceOperationKind::Heal) };
                    Specimen.Reference = { Push };
                    const CorpusMeasures Measure = MeasureCorpus(Which, Scale);
                    Specimen.ExpectedVolume = Measure.Volume + Measure.TopArea * 0.5 * Scale;
                    Specimen.ExpectedGenus = Measure.Genus; Specimen.ExpectedCounts = Measure.Counts;
                    return Specimen;
                } });
                List.push_back({ std::string("ThickenDelete") + BodyNames[Which], [Which](ModelPartition& Partition, double Scale)
                {
                    FaceOperationSpecimen Specimen;
                    Specimen.Name = std::string("ThickenDelete") + BodyNames[Which]; Specimen.Family = "Chain";
                    Specimen.Description = "Heal(Thicken(DeleteFace(+z), 0.1)) ≡ Hollow(0.1, +z): direct rebuild vs General Fuse";
                    Specimen.Body = ConstructCorpusBody(Partition, Which, Scale);
                    const std::vector<FaceToken> Top = SelectFaces(Partition, Specimen.Body, Facing({ 0.0, 0.0, 1.0 }));
                    FaceOperationSettings Thicken = Operation(FaceOperationKind::Thicken), Hollow = Operation(FaceOperationKind::Hollow, Top);
                    Thicken.Distance = Hollow.Distance = 0.1 * Scale;
                    Specimen.Steps = { Operation(FaceOperationKind::Delete, Top), Thicken, Operation(FaceOperationKind::Heal) };
                    Specimen.Reference = { Hollow };
                    const double Wall = 0.1, Cube = Scale * Scale * Scale;
                    const CorpusMeasures Measure = MeasureCorpus(Which, Scale);
                    if (Which == 0) Specimen.ExpectedVolume = (BlockX * BlockY * BlockZ - (BlockX - 2 * Wall) * (BlockY - 2 * Wall) * (BlockZ - Wall)) * Cube;
                    if (Which == 1) Specimen.ExpectedVolume = Measure.Volume - ((SlabX - 2 * Wall) * (SlabY - 2 * Wall) - π * (BoreRadius + Wall) * (BoreRadius + Wall)) * (SlabZ - Wall) * Cube;
                    if (Which == 2) Specimen.ExpectedVolume = Measure.Volume - π * (CanRadius - Wall) * (CanRadius - Wall) * (CanHeight - Wall) * Cube;
                    Specimen.ExpectedGenus = Measure.Genus;
                    return Specimen;
                } });
                List.push_back({ std::string("OffsetBodyOut") + BodyNames[Which], [Which](ModelPartition& Partition, double Scale)
                {
                    FaceOperationSpecimen Specimen;
                    Specimen.Name = std::string("OffsetBodyOut") + BodyNames[Which]; Specimen.Family = "Extra";
                    Specimen.Description = "Every face +0.1 (sharp offset: carriers offset, edges re-intersected)";
                    Specimen.Body = ConstructCorpusBody(Partition, Which, Scale);
                    FaceOperationSettings Settings = Operation(FaceOperationKind::OffsetBody);
                    Settings.Distance = 0.1 * Scale;
                    Specimen.Steps = { Settings };
                    const double Cube = Scale * Scale * Scale, Step = 0.1;
                    if (Which == 0) Specimen.ExpectedVolume = (BlockX + 2 * Step) * (BlockY + 2 * Step) * (BlockZ + 2 * Step) * Cube;
                    if (Which == 1) Specimen.ExpectedVolume = ((SlabX + 2 * Step) * (SlabY + 2 * Step) - π * (BoreRadius - Step) * (BoreRadius - Step)) * (SlabZ + 2 * Step) * Cube;
                    if (Which == 2) Specimen.ExpectedVolume = π * (CanRadius + Step) * (CanRadius + Step) * (CanHeight + 2 * Step) * Cube;
                    const CorpusMeasures Measure = MeasureCorpus(Which, Scale);
                    Specimen.ExpectedGenus = Measure.Genus; Specimen.ExpectedCounts = Measure.Counts;
                    return Specimen;
                } });
                List.push_back({ std::string("OffsetBodyIn") + BodyNames[Which], [Which](ModelPartition& Partition, double Scale)
                {
                    FaceOperationSpecimen Specimen;
                    Specimen.Name = std::string("OffsetBodyIn") + BodyNames[Which]; Specimen.Family = "Extra";
                    Specimen.Description = "Every face −0.1 (the cavity of Hollow, closed)";
                    Specimen.Body = ConstructCorpusBody(Partition, Which, Scale);
                    FaceOperationSettings Settings = Operation(FaceOperationKind::OffsetBody);
                    Settings.Distance = -0.1 * Scale;
                    Specimen.Steps = { Settings };
                    const double Cube = Scale * Scale * Scale, Step = -0.1;
                    if (Which == 0) Specimen.ExpectedVolume = (BlockX + 2 * Step) * (BlockY + 2 * Step) * (BlockZ + 2 * Step) * Cube;
                    if (Which == 1) Specimen.ExpectedVolume = ((SlabX + 2 * Step) * (SlabY + 2 * Step) - π * (BoreRadius - Step) * (BoreRadius - Step)) * (SlabZ + 2 * Step) * Cube;
                    if (Which == 2) Specimen.ExpectedVolume = π * (CanRadius + Step) * (CanRadius + Step) * (CanHeight + 2 * Step) * Cube;
                    const CorpusMeasures Measure = MeasureCorpus(Which, Scale);
                    Specimen.ExpectedGenus = Measure.Genus; Specimen.ExpectedCounts = Measure.Counts;
                    return Specimen;
                } });
            }
            List.push_back({ "DeleteBore", [](ModelPartition& Partition, double Scale)
            {
                FaceOperationSpecimen Specimen;
                Specimen.Name = "DeleteBore"; Specimen.Family = "Extra";
                Specimen.Description = "HoleSlab bore wall removed: both hole loops healed, solid slab (genus 1 → 0)";
                Specimen.Body = ConstructCorpusBody(Partition, 1, Scale);
                Specimen.Steps = { Operation(FaceOperationKind::Delete, SelectFaces(Partition, Specimen.Body, Curved())) };
                Specimen.ExpectedVolume = SlabX * SlabY * SlabZ * Scale * Scale * Scale;
                Specimen.ExpectedGenus = 0; Specimen.ExpectedCounts = { 6, 12, 8 };
                return Specimen;
            } });
            List.push_back({ "DraftAllSides", [](ModelPartition& Partition, double Scale)
            {
                FaceOperationSpecimen Specimen;
                Specimen.Name = "DraftAllSides"; Specimen.Family = "Extra";
                Specimen.Description = "Four Block sides 5°: corner edges between two drafted planes, V = abh + (a+b)h²t + 4h³t²/3";
                Specimen.Body = ConstructCorpusBody(Partition, 0, Scale);
                FaceOperationSettings Settings = Operation(FaceOperationKind::Draft, SelectFaces(Partition, Specimen.Body, Sides()));
                Settings.Angle = 5.0 * π / 180.0;
                Specimen.Steps = { Settings };
                const double Tan5 = std::tan(Settings.Angle), Height = BlockZ;
                Specimen.ExpectedVolume = (BlockX * BlockY * Height + (BlockX + BlockY) * Height * Height * Tan5 + 4.0 * Height * Height * Height * Tan5 * Tan5 / 3.0) * Scale * Scale * Scale;
                Specimen.ExpectedGenus = 0; Specimen.ExpectedCounts = { 6, 12, 8 };
                return Specimen;
            } });
            List.push_back({ "DraftBore", [](ModelPartition& Partition, double Scale)
            {
                FaceOperationSpecimen Specimen;
                Specimen.Name = "DraftBore"; Specimen.Family = "Extra";
                Specimen.Description = "HoleSlab bore 5° (outward normal toward the axis: the hole narrows upward), cylinder → cone";
                Specimen.Body = ConstructCorpusBody(Partition, 1, Scale);
                FaceOperationSettings Settings = Operation(FaceOperationKind::Draft, SelectFaces(Partition, Specimen.Body, Curved()));
                Settings.Angle = 5.0 * π / 180.0;
                Specimen.Steps = { Settings };
                Specimen.ExpectedVolume = (SlabX * SlabY * SlabZ - ConeVolume(BoreRadius, -std::tan(Settings.Angle), SlabZ)) * Scale * Scale * Scale;
                Specimen.ExpectedGenus = 1; Specimen.ExpectedCounts = { 7, 15, 10 };
                return Specimen;
            } });
            List.push_back({ "OffsetBoreWall", [](ModelPartition& Partition, double Scale)
            {
                FaceOperationSpecimen Specimen;
                Specimen.Name = "OffsetBoreWall"; Specimen.Family = "Extra";
                Specimen.Description = "Bore wall −0.1 along its outward normal (toward the axis): the bore widens to r 0.55";
                Specimen.Body = ConstructCorpusBody(Partition, 1, Scale);
                FaceOperationSettings Settings = Operation(FaceOperationKind::Offset, SelectFaces(Partition, Specimen.Body, Curved()));
                Settings.Distance = -0.1 * Scale;
                Specimen.Steps = { Settings };
                Specimen.ExpectedVolume = (SlabX * SlabY - π * 0.55 * 0.55) * SlabZ * Scale * Scale * Scale;
                Specimen.ExpectedGenus = 1; Specimen.ExpectedCounts = { 7, 15, 10 };
                return Specimen;
            } });
            List.push_back({ "ScaleGrowBlock", [](ModelPartition& Partition, double Scale)
            {
                FaceOperationSpecimen Specimen;
                Specimen.Name = "ScaleGrowBlock"; Specimen.Family = "Extra";
                Specimen.Description = "Block +z × 2: sides lean outward, frustum 1/3·(3 + 12 + 6)";
                Specimen.Body = ConstructCorpusBody(Partition, 0, Scale);
                FaceOperationSettings Settings = Operation(FaceOperationKind::Scale, SelectFaces(Partition, Specimen.Body, Facing({ 0.0, 0.0, 1.0 })));
                Settings.Factor = 2.0;
                Specimen.Steps = { Settings };
                Specimen.ExpectedVolume = FrustumVolume(BlockX * BlockY, 4.0 * BlockX * BlockY, BlockZ) * Scale * Scale * Scale;
                Specimen.ExpectedGenus = 0; Specimen.ExpectedCounts = { 6, 12, 8 };
                return Specimen;
            } });
            List.push_back({ "ExtrudeSlabBottom", [](ModelPartition& Partition, double Scale)
            {
                FaceOperationSpecimen Specimen;
                Specimen.Name = "ExtrudeSlabBottom"; Specimen.Family = "Extra";
                Specimen.Description = "HoleSlab −z face 0.3 down: ring of 4 planes + 1 cylinder (seam reused)";
                Specimen.Body = ConstructCorpusBody(Partition, 1, Scale);
                FaceOperationSettings Settings = Operation(FaceOperationKind::Extrude, SelectFaces(Partition, Specimen.Body, Facing({ 0.0, 0.0, -1.0 })));
                Settings.Distance = 0.3 * Scale;
                Specimen.Steps = { Settings };
                const CorpusMeasures Measure = MeasureCorpus(1, Scale);
                Specimen.ExpectedVolume = Measure.Volume + Measure.TopArea * 0.3 * Scale;
                Specimen.ExpectedGenus = 1; Specimen.ExpectedCounts = { 12, 25, 15 };
                return Specimen;
            } });

            // ---- Refusals: the change exceeds what the topology can absorb, or the verb does not apply.
            const auto Refusal = [](const char* Name, const char* Description, int Which, std::function<FaceOperationSettings(ModelPartition&, BodyToken, double)> Make)
            {
                return std::pair<std::string, SpecimenBuilder>{ Name, [=](ModelPartition& Partition, double Scale)
                {
                    FaceOperationSpecimen Specimen;
                    Specimen.Name = Name; Specimen.Family = "Refusal"; Specimen.Description = Description; Specimen.RefusalExpected = true;
                    Specimen.Body  = ConstructCorpusBody(Partition, Which, Scale);
                    Specimen.Steps = { Make(Partition, Specimen.Body, Scale) };
                    return Specimen;
                } };
            };
            List.push_back(Refusal("PushThrough", "Block +z pushed −1.2 (below the base): side edges would reverse", 0, [](ModelPartition& Partition, BodyToken Body, double Scale)
            {
                FaceOperationSettings Settings = Operation(FaceOperationKind::Push, SelectFaces(Partition, Body, Facing({ 0.0, 0.0, 1.0 })));
                Settings.Distance = -1.2 * Scale;
                return Settings;
            }));
            List.push_back(Refusal("TrimCanAway", "Can side trimmed 0.7 > radius 0.6: the offset cylinder degenerates", 2, [](ModelPartition& Partition, BodyToken Body, double Scale)
            {
                FaceOperationSettings Settings = Operation(FaceOperationKind::Trim, SelectFaces(Partition, Body, Facing({ 0.0, 0.0, 1.0 })));
                Settings.Distance = 0.7 * Scale;
                return Settings;
            }));
            List.push_back(Refusal("TrimSlabAcross", "HoleSlab sides trimmed 1.1: the slab's y-extent would reverse", 1, [](ModelPartition& Partition, BodyToken Body, double Scale)
            {
                FaceOperationSettings Settings = Operation(FaceOperationKind::Trim, SelectFaces(Partition, Body, Facing({ 0.0, 0.0, 1.0 })));
                Settings.Distance = 1.1 * Scale;
                return Settings;
            }));
            List.push_back(Refusal("DraftTop", "Draft of the +z face against pull +z: nothing to tilt", 0, [](ModelPartition& Partition, BodyToken Body, double)
            {
                FaceOperationSettings Settings = Operation(FaceOperationKind::Draft, SelectFaces(Partition, Body, Facing({ 0.0, 0.0, 1.0 })));
                Settings.Angle = 5.0 * π / 180.0;
                return Settings;
            }));
            List.push_back(Refusal("HollowCanThick", "Can hollowed with wall 0.7 > radius: the inner cylinder degenerates", 2, [](ModelPartition& Partition, BodyToken Body, double Scale)
            {
                FaceOperationSettings Settings = Operation(FaceOperationKind::Hollow, SelectFaces(Partition, Body, Facing({ 0.0, 0.0, 1.0 })));
                Settings.Distance = 0.7 * Scale;
                return Settings;
            }));
            List.push_back(Refusal("ThickenSolid", "Thicken of a solid (only sheets thicken)", 0, [](ModelPartition&, BodyToken, double Scale)
            {
                FaceOperationSettings Settings = Operation(FaceOperationKind::Thicken);
                Settings.Distance = 0.1 * Scale;
                return Settings;
            }));
            List.push_back(Refusal("ExtrudeCurved", "ExtrudeFaces of the Can side (not planar)", 2, [](ModelPartition& Partition, BodyToken Body, double Scale)
            {
                FaceOperationSettings Settings = Operation(FaceOperationKind::Extrude, SelectFaces(Partition, Body, Curved()));
                Settings.Distance = 0.2 * Scale;
                return Settings;
            }));
            List.push_back(Refusal("RotateCanSteep", "Can top turned 60°: the tilted plane cuts the base inside, the seam would reverse", 2, [](ModelPartition& Partition, BodyToken Body, double)
            {
                FaceOperationSettings Settings = Operation(FaceOperationKind::Rotate, SelectFaces(Partition, Body, Facing({ 0.0, 0.0, 1.0 })));
                Settings.Angle = 60.0 * π / 180.0;
                return Settings;
            }));
            return List;
        }();
        return Specimens;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

BodyToken ConstructCorpusBody(ModelPartition& Partition, int Which, double Scale)
{
    const OrthonormalFrame Upright{ {}, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    if (Which == 0) return ConstructBlock(Partition, {}, Vector3{ BlockX, BlockY, BlockZ } * Scale).Value;
    if (Which == 1)
    {
        OrthonormalFrame Bore = Upright;
        Bore.Origin = Vector3{ 0.5 * SlabX, 0.5 * SlabY, -0.5 } * Scale;
        return ComputeBoolean(Partition, ConstructBlock(Partition, {}, Vector3{ SlabX, SlabY, SlabZ } * Scale).Value, ConstructCylinder(Partition, Bore, BoreRadius * Scale, 2.0 * Scale).Value,
                              BooleanOperation::Difference).Body;
    }
    return ConstructCylinder(Partition, Upright, CanRadius * Scale, CanHeight * Scale).Value;
}

std::vector<std::string> EnumerateFaceOperationSpecimens()
{
    std::vector<std::string> Names;
    for (const CorpusRow& Row : CorpusRows) for (const char* Body : BodyNames) Names.push_back(std::string(Row.Verb) + Body);
    for (const auto& [Name, Builder] : FurtherSpecimens()) Names.push_back(Name);
    return Names;
}

FaceOperationSpecimen ConstructFaceOperationSpecimen(ModelPartition& Partition, const std::string& Name, double Scale)
{
    for (const CorpusRow& Row : CorpusRows)
    {
        for (int Which = 0; Which < 3; ++Which) if (Name == std::string(Row.Verb) + BodyNames[Which]) return ConstructCorpusSpecimen(Partition, Row, Which, Scale);
    }
    for (const auto& [Entry, Builder] : FurtherSpecimens()) if (Entry == Name) return Builder(Partition, Scale);
    return {};
}

} // namespace Frontier::GeometrySpace::Parametric
