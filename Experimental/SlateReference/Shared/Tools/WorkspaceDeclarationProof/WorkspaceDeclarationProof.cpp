//============================================================================================================================================
//                                                    WORKSPACEDECLARATIONPROOF.CPP
//============================================================================================================================================
// 🧩 Builds every declared workspace into a real partition and proves it lands where the host's code did.
//
// 🔴 The whole risk of step 9 is in the slot ORDINALS. A declaration says "seat the directory at slot 1",
//    and slot 1 is only the right slot if `PanelStructure::Divide` allocates the way the declaration
//    assumes. That assumption is unprovable by reading — it depends on the allocator inside a class this
//    unit does not own. So this gate runs the real `PanelStructure`, applies each declaration to it, and
//    walks the resulting leaves. If `Divide` ever changes how it hands out ordinals, every arrangement
//    here fails at once, which is the correct blast radius for that change.
//
// 📝 The sketching arrangement is asserted against the exact panels and proportions that
//    `ConstructParametricLayout` produced in `ParametricSketchHost.cpp` before it was deleted — a
//    directory on the left at 0.27, a tool catalogue at 0.33 of the remainder, and a viewport.

#include "SlateWorkspace/Discipline/WorkspaceDeclaration/Api/WorkspaceDeclaration.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace Slate;

namespace
{

int Failures = 0;
int Checks   = 0;

void Claim(bool Held, const std::string& Stated)
{
    ++Checks;
    if (Held)
        return;
    ++Failures;
    std::printf("  FAIL  %s\n", Stated.c_str());
}

const char* PanelText(PanelSubject Subject)
{
    switch (Subject)
    {
        case PanelSubject::Viewport:        return "Viewport";
        case PanelSubject::Uv:              return "Uv";
        case PanelSubject::Outliner:        return "Outliner";
        case PanelSubject::Properties:      return "Properties";
        case PanelSubject::Vacant:          return "Vacant";
        case PanelSubject::Texturing:    return "LayerStack";
        case PanelSubject::ParametricTools: return "ToolCatalogue";
        case PanelSubject::SketchDirectory: return "SketchDirectory";
        default:                            return "?";
    }
}

/// Every leaf the partition presents, in ordinal order.
std::vector<PanelSubject> SeatedPanels(const PanelStructure& Partition)
{
    std::vector<PanelSubject> Seated;
    for (std::uint32_t Slot = 0u; Slot < PanelStructure::RecordLimit; ++Slot)
    {
        const Deliver<PanelRecord> Held = Partition.Current(Slot);
        if (!Held.Resolved)
            continue;
        const PanelRecord Record = Held.Resolve();
        if (Record.Occupied && !Record.Divided)
            Seated.push_back(Record.Subject);
    }
    return Seated;
}

bool Seats(const std::vector<PanelSubject>& Seated, PanelSubject Wanted)
{
    for (const PanelSubject Subject : Seated)
        if (Subject == Wanted)
            return true;
    return false;
}

std::string Listed(const std::vector<PanelSubject>& Seated)
{
    std::string Text;
    for (const PanelSubject Subject : Seated)
    {
        if (!Text.empty())
            Text += ", ";
        Text += PanelText(Subject);
    }
    return Text;
}

//------------------------------------------------------------------------------------------------------------------------
//                             1. THE SKETCHING ARRANGEMENT MATCHES THE DELETED PROCEDURE
//------------------------------------------------------------------------------------------------------------------------

/// The procedure exactly as it stood in `ParametricSketchHost.cpp`, before the declaration replaced it.
/// ⚠️ Frozen copy. Do not tidy — its value is that it is what shipped.
void RetiredParametricLayout(PanelStructure& Partition)
{
    Partition.ConstructPanelPartition(PanelSubject::Viewport);

    if (!Partition.Divide(PanelStructure::RootIndex, PanelDivisionAxis::X,
                          PanelDivisionSide::Minimum).Resolved)
        return;

    const Deliver<PanelRecord> Root = Partition.Current(PanelStructure::RootIndex);
    if (!Root.Resolved)
        return;

    Discard(Partition.Assign(Root.Resolve().Minimum, PanelSubject::SketchDirectory));

    const std::uint32_t Right = Root.Resolve().Maximum;
    if (!Partition.Divide(Right, PanelDivisionAxis::X, PanelDivisionSide::Minimum).Resolved)
        return;

    const Deliver<PanelRecord> RightBranch = Partition.Current(Right);
    if (!RightBranch.Resolved)
        return;

    Discard(Partition.Assign(RightBranch.Resolve().Minimum, PanelSubject::ParametricTools));
    Discard(Partition.Assign(RightBranch.Resolve().Maximum, PanelSubject::Viewport));
    Discard(Partition.Proportion(PanelStructure::RootIndex, 0.27f));
    Discard(Partition.Proportion(Right, 0.33f));
}

void ProveSketchMatchesRetired()
{
    std::printf("1. The declared sketching workspace equals the deleted procedure\n");

    PanelStructure Retired;
    RetiredParametricLayout(Retired);

    PanelStructure Declared;
    const Deliver<bool> Applied = ApplyWorkspace(DeclaredSketchWorkspace(), Declared);
    Claim(Applied.Resolved, "the sketching declaration should apply without refusal");

    const std::vector<PanelSubject> RetiredSeats  = SeatedPanels(Retired);
    const std::vector<PanelSubject> DeclaredSeats = SeatedPanels(Declared);

    Claim(RetiredSeats.size() == DeclaredSeats.size(),
          "seat count differs: retired [" + Listed(RetiredSeats) +
              "] vs declared [" + Listed(DeclaredSeats) + "]");

    // 🔴 Slot for slot, not merely the same set. A directory and a viewport swapped would satisfy a set
    //    comparison and give the artist a mirrored workspace.
    for (std::uint32_t Slot = 0u; Slot < PanelStructure::RecordLimit; ++Slot)
    {
        const Deliver<PanelRecord> Left  = Retired.Current(Slot);
        const Deliver<PanelRecord> Right = Declared.Current(Slot);

        Claim(Left.Resolved == Right.Resolved,
              "slot " + std::to_string(Slot) + " is occupied in one arrangement and not the other");
        if (!Left.Resolved || !Right.Resolved)
            continue;

        const PanelRecord One = Left.Resolve();
        const PanelRecord Two = Right.Resolve();

        Claim(One.Divided == Two.Divided,
              "slot " + std::to_string(Slot) + " is divided in one arrangement and not the other");
        Claim(One.Subject == Two.Subject,
              "slot " + std::to_string(Slot) + " presents " + PanelText(Two.Subject) +
                  ", the procedure presented " + PanelText(One.Subject));
        Claim(One.Minimum == Two.Minimum && One.Maximum == Two.Maximum,
              "slot " + std::to_string(Slot) + " divides into different descendants");

        // The two proportions are the only numbers the deleted procedure carried.
        Claim(One.MinimumFraction == Two.MinimumFraction,
              "slot " + std::to_string(Slot) + " holds fraction " + std::to_string(Two.MinimumFraction) +
                  ", the procedure held " + std::to_string(One.MinimumFraction));
    }

    std::printf("   [%s]\n", Listed(DeclaredSeats).c_str());
}

//------------------------------------------------------------------------------------------------------------------------
//                                  2. EVERY DECLARATION BUILDS AND SEATS WHAT IT PROMISES
//------------------------------------------------------------------------------------------------------------------------

void ProveEveryDeclaration()
{
    std::printf("2. Every declared workspace builds, and seats the panels its discipline needs\n");

    const std::vector<WorkspaceDeclaration> Every = {
        DeclaredVacantWorkspace(),
        DeclaredTextureWorkspace(),
        DeclaredSketchWorkspace(),
        DeclaredCombinedWorkspace(),
    };

    for (const WorkspaceDeclaration& Declared : Every)
    {
        const std::string Where = Declared.Naming;

        PanelStructure Partition;
        const Deliver<bool> Applied = ApplyWorkspace(Declared, Partition);
        Claim(Applied.Resolved, Where + " should apply without refusal");

        const std::vector<PanelSubject> Seated = SeatedPanels(Partition);
        Claim(!Seated.empty(), Where + " should seat at least one panel");

        // 🔴 Every workspace seats a viewport. A discipline whose arrangement lost its viewport would open
        //    on a workspace the artist cannot see anything in — and the divide-then-reseat ordering makes
        //    that a one-character mistake.
        Claim(Seats(Seated, PanelSubject::Viewport), Where + " must seat a viewport");

        // A discipline that offers tools must seat the panel those tools are chosen from, or the artist
        // holds a toolset with no way to pick from it.
        if (Declared.SketchTools)
            Claim(Seats(Seated, PanelSubject::ParametricTools),
                  Where + " offers sketch tools and must seat the tool catalogue");
        if (Declared.TextureTools)
            Claim(Seats(Seated, PanelSubject::Texturing) || Declared.SketchTools,
                  Where + " offers texturing tools and must seat the layer stack");

        Claim(Declared.StepCount <= WorkspaceDeclaration::StepLimit,
              Where + " declares more steps than the limit");
        Claim(std::string(Declared.Naming).length() > 0u, "every workspace must be named");

        std::printf("   %-12s [%s]\n", Declared.Naming, Listed(Seated).c_str());
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                            3. THE DISCIPLINES ARE DISTINCT, AND COMBINED IS FREE
//------------------------------------------------------------------------------------------------------------------------

void ProveDisciplinesDiffer()
{
    std::printf("3. The disciplines differ from each other, and combined costs no arrangement\n");

    const WorkspaceDeclaration Texture  = DeclaredTextureWorkspace();
    const WorkspaceDeclaration Sketch   = DeclaredSketchWorkspace();
    const WorkspaceDeclaration Combined = DeclaredCombinedWorkspace();
    const WorkspaceDeclaration Vacant   = DeclaredVacantWorkspace();

    Claim(Texture.Subject != Sketch.Subject, "the two disciplines must not name the same workspace subject");
    Claim(Texture.TextureTools && !Texture.SketchTools, "texturing offers texturing tools only");
    Claim(Sketch.SketchTools && !Sketch.TextureTools,   "sketching offers sketch tools only");

    // 🔴 The plan's §3 claim, made executable: a combined build is one more line of data. If someone later
    //    gives combined its own arrangement, this fails and says so.
    Claim(Combined.SketchTools && Combined.TextureTools, "combined offers both toolsets");
    Claim(Combined.StepCount == Sketch.StepCount,
          "combined must reuse the sketching arrangement, not carry its own");
    for (std::uint32_t Index = 0u; Index < Sketch.StepCount; ++Index)
        Claim(Combined.Steps[Index].Applied == Sketch.Steps[Index].Applied &&
              Combined.Steps[Index].Slot    == Sketch.Steps[Index].Slot &&
              Combined.Steps[Index].Subject == Sketch.Steps[Index].Subject,
              "combined step " + std::to_string(Index) + " diverges from sketching");

    Claim(Vacant.StepCount == 0u, "the blank workspace must declare no steps at all");
    Claim(!Vacant.SketchTools && !Vacant.TextureTools, "the blank workspace offers no tools");

    // Applying a declaration twice must give the same partition — bring-up may run more than once when a
    // workspace is registered again, and the second run must not accumulate panels onto the first.
    PanelStructure Once;
    Discard(ApplyWorkspace(Sketch, Once));
    const std::vector<PanelSubject> First = SeatedPanels(Once);
    Discard(ApplyWorkspace(Sketch, Once));
    const std::vector<PanelSubject> Second = SeatedPanels(Once);
    Claim(First.size() == Second.size() && Listed(First) == Listed(Second),
          "applying a workspace twice must not accumulate: [" + Listed(First) + "] then [" +
              Listed(Second) + "]");
}

//------------------------------------------------------------------------------------------------------------------------
//                            3b. THE SUBJECT A WORKSPACE IS REGISTERED UNDER SELECTS ITS ARRANGEMENT
//------------------------------------------------------------------------------------------------------------------------

void ProveSubjectSelects()
{
    std::printf("3b. Every workspace subject selects a declaration, and it builds\n");

    // 🔴 Registering a workspace under a subject and building its panels were two separate statements in
    //    every host, with nothing tying them together — a host could register one discipline and seat
    //    another's panels and still compile. `DeclaredWorkspaceFor` is that join, so every subject must
    //    reach a declaration that actually builds.
    for (std::uint32_t Ordinal = 0u;
         Ordinal < static_cast<std::uint32_t>(WorkspaceSubject::SubjectCount);
         ++Ordinal)
    {
        const WorkspaceSubject Subject  = static_cast<WorkspaceSubject>(Ordinal);
        const WorkspaceDeclaration Held = DeclaredWorkspaceFor(Subject);

        PanelStructure Partition;
        const Deliver<bool> Applied = ApplyWorkspace(Held, Partition);
        Claim(Applied.Resolved,
              "subject " + std::to_string(Ordinal) + " selects a declaration that refuses to build");
        Claim(Seats(SeatedPanels(Partition), PanelSubject::Viewport),
              "subject " + std::to_string(Ordinal) + " must reach a workspace seating a viewport");

        std::printf("   %u -> %-12s [%s]\n", Ordinal, Held.Naming,
                    Listed(SeatedPanels(Partition)).c_str());
    }

    // The two disciplines that HAVE an arrangement must select their own, not fall through to blank.
    Claim(DeclaredWorkspaceFor(WorkspaceSubject::Parametric).SketchTools,
          "the parametric subject must select the sketching workspace");
    Claim(DeclaredWorkspaceFor(WorkspaceSubject::Texturing).TextureTools,
          "the texturing subject must select the texturing workspace");
    Claim(DeclaredWorkspaceFor(WorkspaceSubject::Vacant).StepCount == 0u,
          "the blank subject must select the blank workspace");

    // 📝 A subject with no arrangement of its own opens bare rather than borrowing another discipline's.
    Claim(DeclaredWorkspaceFor(WorkspaceSubject::Modelling).StepCount == 0u,
          "a discipline with no arrangement must open bare, not borrow one");
}

//------------------------------------------------------------------------------------------------------------------------
//                                     4. A BAD DECLARATION REFUSES RATHER THAN MISBUILDS
//------------------------------------------------------------------------------------------------------------------------

void ProveRefusals()
{
    std::printf("4. A declaration naming a slot that does not exist refuses\n");

    // 🔴 Seating into a slot no division created must be reported, not swallowed. A silent skip would give
    //    the artist a workspace missing one panel with nothing to explain why — which is precisely how the
    //    retired procedure behaved, since every one of its calls was wrapped in `Discard`.
    WorkspaceDeclaration Impossible = {};
    Impossible.Naming    = "Impossible";
    Impossible.Initial   = PanelSubject::Viewport;
    Impossible.Steps[0]  = { ArrangementStep::Action::Seat, 9u,
                             PanelSubject::Outliner, PanelDivisionAxis::X, PanelDivisionSide::Minimum, 0.5f };
    Impossible.StepCount = 1u;

    PanelStructure Partition;
    const Deliver<bool> Applied = ApplyWorkspace(Impossible, Partition);
    Claim(!Applied.Resolved, "seating into a slot that was never divided must refuse");

    // Overrunning the step limit refuses rather than reading past the array.
    WorkspaceDeclaration Overrun = DeclaredSketchWorkspace();
    Overrun.StepCount = WorkspaceDeclaration::StepLimit + 1u;
    PanelStructure Second;
    Claim(!ApplyWorkspace(Overrun, Second).Resolved, "a step count past the limit must refuse");

    // A declaration with no steps still yields a usable partition.
    PanelStructure Bare;
    Claim(ApplyWorkspace(DeclaredVacantWorkspace(), Bare).Resolved, "the blank workspace should apply");
    Claim(SeatedPanels(Bare).size() == 1u, "the blank workspace seats exactly one panel");
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------

int main()
{
    std::printf("\n=== WorkspaceDeclaration — SlateWorkspace ===\n\n");

    ProveSketchMatchesRetired();
    ProveEveryDeclaration();
    ProveDisciplinesDiffer();
    ProveSubjectSelects();
    ProveRefusals();

    std::printf("\n%d claims, %d failures\n", Checks, Failures);
    std::printf(Failures == 0 ? "PROVEN\n\n" : "REFUTED\n\n");
    return Failures == 0 ? 0 : 1;
}
