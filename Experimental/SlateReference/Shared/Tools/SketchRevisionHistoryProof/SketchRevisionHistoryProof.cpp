#include "SlateWorkspace/Discipline/SketchRevisionHistory/Api/SketchRevisionHistory.h"

#include <cstdio>

using namespace Slate;

namespace
{
std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

void Claim(bool Held, const char* Sentence)
{
    ++Claims;
    if (!Held)
    {
        ++Failures;
        std::printf("  FAILED  %s\n", Sentence);
    }
}
}

int main()
{
    std::printf("=========================================================================\n");
    std::printf("SKETCH REVISION HISTORY PROOF\n");
    std::printf("=========================================================================\n\n");

    WorkspaceNameIndex Naming = {};
    SketchStructure Sketch = {};
    WorldSketchStructure World = {};
    WorldSketchMapping Mapping = {};
    WorkspaceRecordStructure Records = {};
    WorkspaceRevisionSequence Revisions = {};
    WorkplaneCatalogue Workplanes = {};
    WorkspaceRecordName Pending = {};
    SketchPick Semantic = {};
    std::vector<SketchRevisionSnapshot> Retreated;
    std::vector<SketchRevisionSnapshot> Reinstated;

    Sketch.DeclarePlane({ { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } });
    const SketchCurveName First = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 });
    const WorldCurveName WorldFirst = World.DeclareLine({ 0.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 });
    Mapping.Curves.push_back({ WorldFirst, First });

    RecordSketchRevisionSnapshot(
        ResolveSketchRevisionSnapshot(Naming, Sketch, World, Mapping, Records, Revisions,
                                      Workplanes, Pending, Semantic),
        Retreated, Reinstated);
    Claim(Retreated.size() == 1u,
          "the initial world-backed sketch state becomes the first revision snapshot");

    const SketchCurveName Second = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 0.0, 10.0, 0.0 });
    const WorldCurveName WorldSecond = World.DeclareLine({ 0.0, 0.0, 0.0 }, { 0.0, 10.0, 0.0 });
    Mapping.Curves.push_back({ WorldSecond, Second });
    Revisions.Seal("Declared Line_2", "Create Sketch", {}, 1u);
    RecordSketchRevisionSnapshot(
        ResolveSketchRevisionSnapshot(Naming, Sketch, World, Mapping, Records, Revisions,
                                      Workplanes, Pending, Semantic),
        Retreated, Reinstated);
    Claim(Retreated.size() == 2u && Retreated.back().Revisions.DeclaredCount() == 1u,
          "a new revision is retained with its world and compatibility state");

    Claim(RetreatSketchRevision(Retreated, Reinstated, Naming, Sketch, World, Mapping,
                                Records, Revisions, Workplanes, Pending, Semantic),
          "retreat moves the latest snapshot into the reinstate stack");
    Claim(Sketch.Curves().size() == 1u && World.CurveCount() == 1u
       && Mapping.Curves.size() == 1u && Revisions.DeclaredCount() == 0u,
          "retreat restores the earlier sketch, world, mapping, and revision state together");

    Claim(ReinstateSketchRevision(Retreated, Reinstated, Naming, Sketch, World, Mapping,
                                  Records, Revisions, Workplanes, Pending, Semantic),
          "reinstate returns the latest snapshot");
    Claim(Sketch.Curves().size() == 2u && World.CurveCount() == 2u
       && Mapping.Curves.size() == 2u && Revisions.DeclaredCount() == 1u,
          "reinstate restores the complete world-backed edit");

    RecordSketchRevisionSnapshot(
        ResolveSketchRevisionSnapshot(Naming, Sketch, World, Mapping, Records, Revisions,
                                      Workplanes, Pending, Semantic),
        Retreated, Reinstated);
    Claim(Retreated.size() == 2u,
          "unchanged revision state does not create duplicate snapshots");

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
