//============================================================================================================================================
//                                                           EDITORPANEL.CPP
//============================================================================================================================================
// 🧩 Exact editor chrome and bounded split interaction around skeletal workspace render targets.

#include "SlateUI/Interface/EditorPanel/Api/EditorPanel.h"

#include <cmath>

namespace Slate
{

namespace
{

const char* SubjectTitle(PanelSubject Subject)
{
    switch (Subject)
    {
        case PanelSubject::Viewport:       return "3D Viewport";
        case PanelSubject::Uv:             return "UV Editor";
        case PanelSubject::Outliner:       return "Scene Directory";
        case PanelSubject::Properties:     return "Properties";
        case PanelSubject::Texturing:   return "Layer Stack";
        case PanelSubject::ParametricTools:return "Parametric Tools";
        case PanelSubject::SketchDirectory:return "Sketch Directory";
        case PanelSubject::Vacant:         return "Choose Panel Type";
        default:                           return "Choose Panel Type";
    }
}

const char* ShadingTitle(PanelShading Shading)
{
    switch (Shading)
    {
        case PanelShading::Lit:              return "lit";
        case PanelShading::Matcap:           return "matcap";
        case PanelShading::SourceWire:       return "source wire";
        case PanelShading::TriangulatedWire: return "tri wire";
        case PanelShading::Points:           return "points";
        case PanelShading::Normal:           return "normal";
        case PanelShading::Metallic:         return "metallic";
        case PanelShading::Illumination:     return "gi";
        default:                             return "lit";
    }
}

const char* GizmoTitle(PanelGizmo Gizmo)
{
    return Gizmo == PanelGizmo::Cad ? "cad" : "blender";
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> EditorPanel::ConstructEditorPanel(MotionIntegrator& IncomingMotion,
                                     RecordingSurface& IncomingSurface,
                                     const ThemeProfile& IncomingAppearance)
{
    if (Motion != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "an editor panel construction stands" });

    Motion     = &IncomingMotion;
    Surface    = &IncomingSurface;
    Appearance = &IncomingAppearance;

    if (!Interaction.AttachMotion(IncomingMotion).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "editor panel interaction was rejected" });

    if (!SharedControls.ConstructComponents(Interaction, IncomingSurface, IncomingAppearance).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "shared editor controls were rejected" });

    if (!ScenePresentation.ConstructLeafPanel(IncomingSurface, IncomingAppearance, LeafSubject::Scene).Resolved ||
        !UvPresentation.ConstructLeafPanel(IncomingSurface, IncomingAppearance, LeafSubject::Uv).Resolved ||
        !OutlinerPresentation.ConstructLeafPanel(IncomingSurface, IncomingAppearance, LeafSubject::Outliner).Resolved ||
        !PropertyPresentation.ConstructLeafPanel(IncomingSurface, IncomingAppearance, LeafSubject::Property).Resolved)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "an editor leaf panel was rejected" });
    }

    for (std::uint32_t Index = 0u; Index < ControlCapacity; ++Index)
    {
        const Deliver<ControlIdentity> Registered = Interaction.Register();
        if (!Registered.Resolved)
            return Deliver<bool>::Refuse(Registered.Error);

        Controls[Index] = Registered.Resolve();
    }

    return Deliver<bool>::Result(true);
}

void EditorPanel::Advance(const PointerCondition& Sampled, double Elapsed)
{
    Pointer = Sampled;
    Interaction.Advance(Sampled, Elapsed);
    SharedControls.Sample(Sampled);

    if (!Sampled.ContactHeld && !Sampled.ContactReleased)
        CapturedPresentation = AbsentPresentation;
}

std::uint32_t EditorPanel::ResolveControlIndex(std::uint32_t RecordIndex, ControlRole Role) const
{
    return RecordIndex * ControlsPerRecord + static_cast<std::uint32_t>(Role);
}

bool EditorPanel::Pressed(std::uint32_t Index, const PlaneExtent& Extent, bool PopupAction)
{
    if (Index >= ControlCapacity)
        return false;

    const ControlIdentity Target = Controls[Index];
    const bool DividerPresent = DeferredDivider.Width() > 0.0f && DeferredDivider.Height() > 0.0f;
    const bool WithinExtent = !DividerPresent ||
                                DeferredDivider.Encloses(Pointer.PositionX, Pointer.PositionY);
    const bool Hovered = WithinExtent && Extent.Encloses(Pointer.PositionX, Pointer.PositionY);
    if (Hovered && Pointer.ContactPressed && (PopupAction || !Interaction.AnyDisclosed()) &&
        Interaction.Grab(Target, ControlPart::Body))
    {
        CapturedPresentation = CurrentPresentation;
    }

    Interaction.DeclareHovered(Target, Hovered, 130.0);
    return CapturedPresentation == CurrentPresentation && Interaction.Released(Target) && Hovered;
}

bool EditorPanel::Disclosed(ControlIdentity Target) const
{
    return DisclosedPresentation == CurrentPresentation && Interaction.Disclosed(Target);
}

void EditorPanel::Disclose(ControlIdentity Target)
{
    if (Interaction.Disclose(Target))
        DisclosedPresentation = CurrentPresentation;
}

void EditorPanel::CloseDisclosure()
{
    Interaction.Withdraw();
    DisclosedPresentation = AbsentPresentation;
}

void EditorPanel::Symbol(const PlaneExtent& Extent, ThemeToken Colour)
{
    Surface->Stroke(SymbolSubject::PlaceholderMark, Extent, Colour);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PARTITION RECORDING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> EditorPanel::Record(const PlaneExtent& Extent,
                                  PanelStructure& Partition,
                                  EditorPanelConfiguration& Configuration,
                                  std::uint32_t PresentationIndex,
                                  bool DeferPopups)
{
    if (Surface == nullptr || Appearance == nullptr || Motion == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no editor panel construction stands" });

    if (!Partition.Current(PanelStructure::RootIndex).Resolved)
        Partition.ConstructPanelPartition();

    CurrentPresentation = PresentationIndex;
    DeferredAnchor       = {};
    DeferredDivider     = {};
    DeferredRecord       = PanelStructure::RecordLimit;
    DeferredRole         = ControlRole::RoleCount;
    DisclosedExtent      = {};
    LeafTally            = 0u;

    Surface->Ground(Extent, Appearance->EditorPanel.WindowGround);
    RecordBranch(PanelStructure::RootIndex, Extent, Partition, Configuration);

    // 🔴 The popups are deferred when the caller fills the leaves itself: recorded before the leaf
    //    content, a split or subject menu is textured over by the caller's sky quad and becomes
    //    unreadable. The host records its content between the two calls.
    if (!DeferPopups)
        RecordDeferred(Partition, Configuration);

    return Deliver<bool>::Result(true);
}

void EditorPanel::RecordDeferredPopups(PanelStructure& Partition, EditorPanelConfiguration& Configuration)
{
    if (Surface != nullptr)
        Discard(Surface->SwitchLayer(RecordingSurface::ShellLayer::Above));
    RecordDeferred(Partition, Configuration);
    if (Surface != nullptr)
        Discard(Surface->SwitchLayer(RecordingSurface::ShellLayer::Beneath));
}

bool EditorPanel::PointerCaptured(std::uint32_t PresentationIndex) const
{
    const bool ContactCurrent = Pointer.ContactHeld || Pointer.ContactReleased;
    const bool PresentationCaptured = CapturedPresentation == PresentationIndex;
    const bool PointerWithinPresentation = DeferredDivider.Encloses(Pointer.PositionX, Pointer.PositionY);
    const bool PopupCaptured = DisclosedPresentation == PresentationIndex && Interaction.AnyDisclosed() &&
                               PointerWithinPresentation;
    return ContactCurrent && (PresentationCaptured || PopupCaptured);
}

void EditorPanel::WithdrawPresentation(std::uint32_t PresentationIndex)
{
    if (DisclosedPresentation == PresentationIndex)
        CloseDisclosure();
    else if (DisclosedPresentation != AbsentPresentation && DisclosedPresentation > PresentationIndex)
        --DisclosedPresentation;

    if (CapturedPresentation == PresentationIndex)
    {
        Interaction.Abandon();
        CapturedPresentation = AbsentPresentation;
    }
    else if (CapturedPresentation != AbsentPresentation && CapturedPresentation > PresentationIndex)
    {
        --CapturedPresentation;
    }
}

void EditorPanel::RecordBranch(std::uint32_t RecordIndex,
                               const PlaneExtent& Extent,
                               PanelStructure& Partition,
                               EditorPanelConfiguration& Configuration)
{
    const Deliver<PanelRecord> Delivered = Partition.Current(RecordIndex);
    if (!Delivered.Resolved)
        return;

    const PanelRecord Declared = Delivered.Resolve();
    if (!Declared.Divided)
    {
        RecordLeaf(RecordIndex, Declared, Extent, Partition, Configuration);
        return;
    }

    const EditorPanelMetric& Measure = Appearance->EditorPanelMeasure;
    const EditorPanelColour&    Colour     = Appearance->EditorPanel;
    const bool X = Declared.Axis == PanelDivisionAxis::X;
    const float Span = X ? Extent.Width() : Extent.Height();
    const float Available = (Span > Measure.SplitterHeight) ? Span - Measure.SplitterHeight : 0.0f;
    const float MinimumSpan = Available * Declared.MinimumFraction;

    PlaneExtent MinimumExtent = Extent;
    PlaneExtent SplitExtent = Extent;
    PlaneExtent MaximumExtent  = Extent;

    if (X)
    {
        MinimumExtent.MaximumX = Extent.MinimumX + MinimumSpan;
        SplitExtent.MinimumX = MinimumExtent.MaximumX;
        SplitExtent.MaximumX = SplitExtent.MinimumX + Measure.SplitterHeight;
        MaximumExtent.MinimumX = SplitExtent.MaximumX;
    }
    else
    {
        MinimumExtent.MaximumY = Extent.MinimumY + MinimumSpan;
        SplitExtent.MinimumY = MinimumExtent.MaximumY;
        SplitExtent.MaximumY = SplitExtent.MinimumY + Measure.SplitterHeight;
        MaximumExtent.MinimumY = SplitExtent.MaximumY;
    }

    const std::uint32_t SplitControl = ResolveControlIndex(RecordIndex, ControlRole::DivisionMenu);
    const ControlIdentity Target = Controls[SplitControl];
    const bool Hovered = SplitExtent.Encloses(Pointer.PositionX, Pointer.PositionY);
    if (Hovered && Pointer.ContactPressed && !Interaction.AnyDisclosed() &&
        Interaction.Grab(Target, ControlPart::Body))
    {
        Interaction.RecordInitial(Target, Declared.MinimumFraction);
        CapturedPresentation = CurrentPresentation;
        DraggedDivision      = RecordIndex;
        DraggedExtent        = Extent;
    }

    const bool DivisionCaptured = CapturedPresentation == CurrentPresentation;
    const bool DivisionHeld = DivisionCaptured && Interaction.Holding(Target);
    const bool DivisionReleased = DivisionCaptured && Interaction.Released(Target);
    Interaction.DeclareHovered(Target, Hovered || DivisionHeld, 130.0);

    float RequestedFraction = Declared.MinimumFraction;
    if (DivisionHeld || DivisionReleased)
    {
        RequestedFraction = X
                          ? (Pointer.PositionX - DraggedExtent.MinimumX) / DraggedExtent.Width()
                          : (Pointer.PositionY - DraggedExtent.MinimumY) / DraggedExtent.Height();
    }

    if (DivisionReleased)
    {
        if (RequestedFraction < 0.05f)
        {
            Discard(Partition.Withdraw(Declared.Minimum));
            RecordBranch(RecordIndex, Extent, Partition, Configuration);
            return;
        }

        if (RequestedFraction > 0.95f)
        {
            Discard(Partition.Withdraw(Declared.Maximum));
            RecordBranch(RecordIndex, Extent, Partition, Configuration);
            return;
        }
    }

    if (DivisionHeld)
        Discard(Partition.Proportion(RecordIndex, RequestedFraction));

    Surface->Ground(SplitExtent, Hovered || DivisionHeld ? Colour.Accent : Colour.ChromeGround);
    Surface->Edge(SplitExtent, Colour.Edge, Measure.EdgeWeight);

    RecordBranch(Declared.Minimum, MinimumExtent, Partition, Configuration);
    RecordBranch(Declared.Maximum, MaximumExtent, Partition, Configuration);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        LEAF CHROME
//------------------------------------------------------------------------------------------------------------------------

void EditorPanel::RecordLeaf(std::uint32_t RecordIndex,
                             const PanelRecord& Declared,
                             const PlaneExtent& Extent,
                             PanelStructure& Partition,
                             EditorPanelConfiguration& Configuration)
{
    CurrentLeafExtent = Extent;

    if (Declared.Subject == PanelSubject::Vacant)
    {
        RecordVacant(RecordIndex, Extent, Partition);
        return;
    }

    const EditorPanelMetric& Measure = Appearance->EditorPanelMeasure;
    const PlaneExtent Header = Spanning(Extent.MinimumX, Extent.MinimumY,
                                        Extent.Width(), Measure.HeaderHeight);
    const PlaneExtent Footer = Spanning(Extent.MinimumX, Extent.MaximumY - Measure.FooterHeight,
                                        Extent.Width(), Measure.FooterHeight);
    const PlaneExtent Body = { Extent.MinimumX, Header.MaximumY, Extent.MaximumX, Footer.MinimumY };

    // 📝 The leaf is delivered to the host, which fills its body with the leaf's own content — the sky in
    //    a viewport leaf, the scene directory in an outliner or properties leaf. The tally grows in
    //    depth-first order and resets at the top of every `Record`.
    if (LeafTally < PanelStructure::RecordLimit)
    {
        LeafBodies[LeafTally]   = Body;
        LeafSubjects[LeafTally] = Declared.Subject;
        ++LeafTally;
    }

    RecordHeader(RecordIndex, Declared.Subject, Header, Partition);

    // 📝 GPU scene and UV rendering are intentionally absent in this skeleton. Each focused panel owns its
    //    render-target body while `EditorPanel` owns only shared chrome and partition interaction.
    switch (Declared.Subject)
    {
        case PanelSubject::Viewport:   ScenePresentation.Record(Body);    break;
        case PanelSubject::Uv:         UvPresentation.Record(Body);       break;
        case PanelSubject::Outliner:   OutlinerPresentation.Record(Body); break;
        case PanelSubject::Properties: PropertyPresentation.Record(Body); break;
        default:                       break;
    }

    RecordFooter(RecordIndex, Declared.Subject, Footer, Configuration);
}

void EditorPanel::RecordHeader(std::uint32_t RecordIndex,
                               PanelSubject Subject,
                               const PlaneExtent& Extent,
                               PanelStructure& Partition)
{
    const EditorPanelMetric& Measure = Appearance->EditorPanelMeasure;
    const EditorPanelColour&    Colour     = Appearance->EditorPanel;
    Surface->Ground(Extent, Colour.ChromeGround);
    Surface->Ground(Spanning(Extent.MinimumX, Extent.MaximumY - Measure.EdgeWeight,
                             Extent.Width(), Measure.EdgeWeight), Colour.Edge);

    const PlaneExtent SubjectButton = Spanning(Extent.MinimumX + Measure.HeaderPadX,
                                               Extent.MinimumY + 2.0f,
                                               44.0f,
                                               Measure.HeaderAction);
    const std::uint32_t SubjectControl = ResolveControlIndex(RecordIndex, ControlRole::SubjectMenu);
    const bool SubjectOpen = Disclosed(Controls[SubjectControl]);
    if (SubjectOpen)
        Surface->Ground(SubjectButton, Colour.Hovered, 4.0f, CornerAll);

    Symbol(Spanning(SubjectButton.MinimumX + 2.0f,
                    SubjectButton.MinimumY + 7.0f,
                    Measure.HeaderSymbol,
                    Measure.HeaderSymbol), Colour.ColourQuiet);
    Surface->TextRun(SubjectButton.MaximumX - 10.0f,
                     SubjectButton.MinimumY + 8.0f,
                     Colour.ColourFaint,
                     "v",
                     Measure.TextSmall,
                     0.0f,
                     true);

    if (Pressed(SubjectControl, SubjectButton, true))
    {
        if (SubjectOpen)
            CloseDisclosure();
        else
            Disclose(Controls[SubjectControl]);
    }

    if (Disclosed(Controls[SubjectControl]))
    {
        DeferredAnchor   = SubjectButton;
        DeferredDivider = CurrentLeafExtent;
        DeferredRecord   = RecordIndex;
        DeferredRole     = ControlRole::SubjectMenu;
    }

    const bool CanRemove = Partition.RemovalAccepted();
    const float ActionCount = CanRemove ? 2.0f : 1.0f;
    const PlaneExtent DivisionButton = Spanning(Extent.MaximumX - Measure.HeaderPadX -
                                                   Measure.HeaderAction * ActionCount,
                                               Extent.MinimumY + 2.0f,
                                               Measure.HeaderAction,
                                               Measure.HeaderAction);
    const PlaneExtent TitleClip = { SubjectButton.MaximumX + Measure.HeaderTitleGap,
                                    Extent.MinimumY,
                                    DivisionButton.MinimumX - 4.0f,
                                    Extent.MaximumY };
    if (TitleClip.Width() > 0.0f)
    {
        Surface->Confine(TitleClip);
        Surface->TextRunTruncated(TitleClip.MinimumX,
                                  Extent.MinimumY + 10.0f,
                                  TitleClip.MaximumX,
                                  Colour.ColourSecondary,
                                  SubjectTitle(Subject),
                                  Measure.TextSmall,
                                  false);
        Surface->Release();
    }

    const std::uint32_t DivisionControl = ResolveControlIndex(RecordIndex, ControlRole::DivisionMenu);
    const bool DivisionOpen = Disclosed(Controls[DivisionControl]);
    if (DivisionOpen)
        Surface->Ground(DivisionButton, Colour.Hovered, 6.0f, CornerAll);

    Symbol(Spanning(DivisionButton.MinimumX + 7.0f,
                    DivisionButton.MinimumY + 7.0f,
                    Measure.HeaderSymbol,
                    Measure.HeaderSymbol), Colour.ColourQuiet);

    if (Pressed(DivisionControl, DivisionButton, true))
    {
        if (DivisionOpen)
            CloseDisclosure();
        else
            Disclose(Controls[DivisionControl]);
    }

    if (Disclosed(Controls[DivisionControl]))
    {
        DeferredAnchor   = DivisionButton;
        DeferredDivider = CurrentLeafExtent;
        DeferredRecord   = RecordIndex;
        DeferredRole     = ControlRole::DivisionMenu;
    }

    if (CanRemove)
    {
        const PlaneExtent WithdrawalButton = Spanning(DivisionButton.MaximumX,
                                                       DivisionButton.MinimumY,
                                                       Measure.HeaderAction,
                                                       Measure.HeaderAction);
        Symbol(Spanning(WithdrawalButton.MinimumX + 7.0f,
                        WithdrawalButton.MinimumY + 7.0f,
                        Measure.HeaderSymbol,
                        Measure.HeaderSymbol), Colour.ColourQuiet);
        if (Pressed(ResolveControlIndex(RecordIndex, ControlRole::Withdrawal), WithdrawalButton))
            Discard(Partition.Withdraw(RecordIndex));
    }
}

void EditorPanel::RecordFooter(std::uint32_t RecordIndex,
                               PanelSubject Subject,
                               const PlaneExtent& Extent,
                               EditorPanelConfiguration& Configuration)
{
    const EditorPanelMetric& Measure = Appearance->EditorPanelMeasure;
    const EditorPanelColour&    Colour     = Appearance->EditorPanel;
    Surface->Ground(Extent, Colour.ChromeGround);
    Surface->Ground(Spanning(Extent.MinimumX, Extent.MinimumY,
                             Extent.Width(), Measure.EdgeWeight), Colour.Edge);

    if (Subject == PanelSubject::Properties)
    {
        Surface->TextRun(Extent.MinimumX + Measure.FooterPadX,
                         Extent.MinimumY + 18.0f, Colour.ColourFaint,
                         "No active object", Measure.TextSmall, 0.0f, false);
        return;
    }

    float Cursor = Extent.MinimumX + Measure.FooterPadX;
    const auto Pill = [&](const char* Caption, float X) -> PlaneExtent
    {
        const PlaneExtent Button = Spanning(Cursor, Extent.MinimumY + 10.0f, X, Measure.PillY);
        Surface->Ground(Button, Colour.BodyGround, Measure.PillRadius, CornerAll);
        Surface->Edge(Button, Colour.Edge, Measure.EdgeWeight, Measure.PillRadius, CornerAll);
        Symbol(Spanning(Button.MinimumX + 10.0f, Button.MinimumY + 7.0f,
                        Measure.HeaderSymbol, Measure.HeaderSymbol), Colour.ColourQuiet);
        Surface->TextRun(Button.MinimumX + 30.0f, Button.MinimumY + 8.0f,
                         Colour.ColourQuiet, Caption, Measure.TextSmall, 0.0f, false);
        Cursor = Button.MaximumX + Measure.FooterGap;
        return Button;
    };

    // 📐 One footer grammar, specialised by leaf subject. These actions only raise requests; the
    //    owning content panel decides which carousel destination to present.
    if (Subject == PanelSubject::Outliner)
    {
        const PlaneExtent Import = Pill("Import", 88.0f);
        const PlaneExtent Export = Pill("Export", 88.0f);
        if (Pressed(ResolveControlIndex(RecordIndex, ControlRole::CameraMenu), Import))
            Configuration.FooterDemand = EditorFooterDemand::SceneImport;
        if (Pressed(ResolveControlIndex(RecordIndex, ControlRole::OverlayMenu), Export))
            Configuration.FooterDemand = EditorFooterDemand::SceneExport;
        return;
    }

    if (Subject == PanelSubject::Texturing)
    {
        const PlaneExtent Flatten = Pill("Export Flattened", 146.0f);
        const PlaneExtent Export = Pill("Export", 88.0f);
        if (Pressed(ResolveControlIndex(RecordIndex, ControlRole::CameraMenu), Flatten))
            Configuration.FooterDemand = EditorFooterDemand::ExportFlattened;
        if (Pressed(ResolveControlIndex(RecordIndex, ControlRole::OverlayMenu), Export))
            Configuration.FooterDemand = EditorFooterDemand::LayerExport;
        return;
    }

    if (Subject != PanelSubject::Viewport)
        return;

    const PlaneExtent LatticeButton = Pill("Grid", 72.0f);
    const std::uint32_t LatticeControl = ResolveControlIndex(RecordIndex, ControlRole::LatticeMenu);
    const std::uint32_t LatticeTypeControl = ResolveControlIndex(RecordIndex, ControlRole::LatticePresentation);
    // 🔴 SelectionField owns the disclosure while its type roster stands. Treat that child disclosure
    //    as keeping the Grid card alive; otherwise opening the roster replaced its parent and both
    //    vanished on the next frame.
    bool LatticeOpen = Disclosed(Controls[LatticeControl]) ||
                       Disclosed(Controls[LatticeTypeControl]);
    if (Pressed(LatticeControl, LatticeButton, true))
    {
        if (LatticeOpen)
        {
            CloseDisclosure();
            LatticeOpen = false;
        }
        else
        {
            Disclose(Controls[LatticeControl]);
            LatticeOpen = true;
        }
    }

    if (LatticeOpen)
    {
        DeferredAnchor   = LatticeButton;
        DeferredDivider = CurrentLeafExtent;
        DeferredRecord   = RecordIndex;
        DeferredRole     = ControlRole::LatticeMenu;
    }

    const float TrailingFloor = Cursor + 20.0f;
    float Trailing = Extent.MaximumX - Measure.FooterPadX;
    const auto TrailingPill = [&](const char* Caption, float X, ThemeToken Accent) -> PlaneExtent
    {
        Trailing -= X;
        const PlaneExtent Button = Spanning(Trailing, Extent.MinimumY + 10.0f, X, Measure.PillY);
        Surface->Ground(Button, Colour.BodyGround, Measure.PillRadius, CornerAll);
        Surface->Edge(Button, Accent, Measure.EdgeWeight, Measure.PillRadius, CornerAll);
        const float CaptionWidth = Surface->MeasureRun(Caption, Measure.TextSmall);
        Surface->TextRun(Button.MinimumX + (Button.Width() - CaptionWidth) * 0.5f,
                         Button.MinimumY + (Button.Height() - Measure.TextSmall) * 0.5f,
                         Colour.ColourQuiet,
                         Caption,
                         Measure.TextSmall,
                         0.0f,
                         true);
        Trailing = Button.MinimumX - Measure.FooterGap;
        return Button;
    };

    if (Subject == PanelSubject::Viewport && Trailing > TrailingFloor + 250.0f)
    {
        const PlaneExtent GizmoButton = TrailingPill(GizmoTitle(Configuration.Gizmo), 78.0f, Colour.Edge);
        const std::uint32_t GizmoControl = ResolveControlIndex(RecordIndex, ControlRole::Gizmo);
        if (Pressed(GizmoControl, GizmoButton, true))
            Disclose(Controls[GizmoControl]);
        if (Disclosed(Controls[GizmoControl]))
        {
            DeferredAnchor   = GizmoButton;
        DeferredDivider = CurrentLeafExtent;
            DeferredRecord = RecordIndex;
            DeferredRole   = ControlRole::Gizmo;
        }

        const PlaneExtent ShadingButton = TrailingPill(ShadingTitle(Configuration.Shading), 86.0f, Colour.Edge);
        const std::uint32_t ShadingControl = ResolveControlIndex(RecordIndex, ControlRole::Shading);
        if (Pressed(ShadingControl, ShadingButton, true))
            Disclose(Controls[ShadingControl]);
        if (Disclosed(Controls[ShadingControl]))
        {
            DeferredAnchor   = ShadingButton;
        DeferredDivider = CurrentLeafExtent;
            DeferredRecord = RecordIndex;
            DeferredRole   = ControlRole::Shading;
        }

        const PlaneExtent CameraButton = TrailingPill(Configuration.Perspective ? "Persp" : "Ortho", 64.0f, Colour.Edge);
        if (Pressed(ResolveControlIndex(RecordIndex, ControlRole::LatticePresentation), CameraButton))
            Configuration.Perspective = !Configuration.Perspective;

        const bool OverlaysTaken = Configuration.FpsOverlay || Configuration.StorageOverlay || Configuration.RendererOverlay;
        const PlaneExtent OverlayButton = TrailingPill("Overlays", 80.0f,
                                                       OverlaysTaken ? Colour.Positive : Colour.Edge);
        const std::uint32_t OverlayControl = ResolveControlIndex(RecordIndex, ControlRole::OverlayMenu);
        if (Pressed(OverlayControl, OverlayButton, true))
            Disclose(Controls[OverlayControl]);
        if (Disclosed(Controls[OverlayControl]))
        {
            DeferredAnchor   = OverlayButton;
        DeferredDivider = CurrentLeafExtent;
            DeferredRecord = RecordIndex;
            DeferredRole   = ControlRole::OverlayMenu;
        }
    }
    else if (Subject == PanelSubject::Uv && Trailing > TrailingFloor + 100.0f)
    {
        static_cast<void>(TrailingPill("2D View", 72.0f, Colour.Edge));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VACANT PANEL CHOOSER
//------------------------------------------------------------------------------------------------------------------------

void EditorPanel::RecordVacant(std::uint32_t RecordIndex,
                               const PlaneExtent& Extent,
                               PanelStructure& Partition)
{
    const EditorPanelMetric& Measure = Appearance->EditorPanelMeasure;
    const EditorPanelColour&    Colour     = Appearance->EditorPanel;
    Surface->Ground(Extent, Colour.WindowGround);

    if (Partition.RemovalAccepted())
    {
        const PlaneExtent Close = Spanning(Extent.MaximumX - 46.0f, Extent.MinimumY + 16.0f, 30.0f, 30.0f);
        Surface->Ground(Close, Colour.ViewGround, 8.0f, CornerAll);
        Surface->Edge(Close, Colour.Edge, Measure.EdgeWeight, 8.0f, CornerAll);
        Symbol(Spanning(Close.MinimumX + 7.0f, Close.MinimumY + 7.0f,
                        16.0f, 16.0f), Colour.ColourFaint);
        if (Pressed(ResolveControlIndex(RecordIndex, ControlRole::Withdrawal), Close))
            Discard(Partition.Withdraw(RecordIndex));
    }

    const float HorizontalPad = 16.0f;
    const float AvailableX = (Extent.Width() > HorizontalPad * 2.0f)
                               ? Extent.Width() - HorizontalPad * 2.0f : Extent.Width();
    const std::uint32_t Columns = (AvailableX >= Measure.ChooserButtonX * 4.0f +
                                                     Measure.ChooserGap * 3.0f) ? 4u
                                : (AvailableX >= 212.0f) ? 2u : 1u;
    const std::uint32_t Rows = (5u + Columns - 1u) / Columns;
    const float ButtonX = (Measure.ChooserButtonX <
                              (AvailableX - Measure.ChooserGap * static_cast<float>(Columns - 1u)) /
                                  static_cast<float>(Columns))
                            ? Measure.ChooserButtonX
                            : (AvailableX - Measure.ChooserGap * static_cast<float>(Columns - 1u)) /
                                  static_cast<float>(Columns);
    const float AvailableY = (Extent.Height() > 96.0f)
                                ? Extent.Height() - 96.0f : Extent.Height();
    const float ButtonY = (Measure.ChooserButtonHeight <
                               (AvailableY - Measure.ChooserGap * static_cast<float>(Rows - 1u)) /
                                   static_cast<float>(Rows))
                             ? Measure.ChooserButtonHeight
                             : (AvailableY - Measure.ChooserGap * static_cast<float>(Rows - 1u)) /
                                   static_cast<float>(Rows);
    const float TotalX = ButtonX * static_cast<float>(Columns) +
                             Measure.ChooserGap * static_cast<float>(Columns - 1u);
    const float TotalY = ButtonY * static_cast<float>(Rows) +
                              Measure.ChooserGap * static_cast<float>(Rows - 1u);
    const float MinimumX = Extent.MinimumX + (Extent.Width() - TotalX) * 0.5f;
    const float MinimumY = Extent.MinimumY + (Extent.Height() - TotalY) * 0.5f + 14.0f;

    Surface->Confine(Extent);
    Surface->TextRun(Extent.MinimumX + Extent.Width() * 0.5f,
                     MinimumY - 34.0f,
                     Colour.ColourSecondary,
                     "Choose Panel Type",
                     Measure.TextBody,
                     0.0f,
                     true);

    // 🔴 This grid and the dropdown menu are two ways to reach the same choice,
    //    so they must offer the same subjects. They had drifted: the grid listed
    //    Properties and never listed the Layer Stack, while the menu listed both.
    const PanelSubject Subjects[6] = { PanelSubject::Viewport, PanelSubject::Uv,
                                       PanelSubject::Outliner, PanelSubject::SketchDirectory,
                                       PanelSubject::ParametricTools, PanelSubject::Texturing };
    const ControlRole Roles[6] = { ControlRole::ChooseViewport, ControlRole::ChooseUv,
                                   ControlRole::ChooseOutliner, ControlRole::ChooseSketchDirectory,
                                   ControlRole::ChooseParametricTools, ControlRole::ChooseTexturing };

    for (std::uint32_t Index = 0u; Index < 6u; ++Index)
    {
        const std::uint32_t Column = Index % Columns;
        const std::uint32_t Row = Index / Columns;
        const PlaneExtent Button = Spanning(MinimumX + static_cast<float>(Column) *
                                                        (ButtonX + Measure.ChooserGap),
                                            MinimumY + static_cast<float>(Row) *
                                                        (ButtonY + Measure.ChooserGap),
                                            ButtonX,
                                            ButtonY);
        Surface->Ground(Button, Colour.BodyGround, Measure.ChooserRadius, CornerAll);
        Surface->Edge(Button, Colour.Edge, Measure.EdgeWeight, Measure.ChooserRadius, CornerAll);
        const float SymbolY = Button.MinimumY + ((ButtonY > 64.0f) ? 18.0f : 8.0f);
        Symbol(Spanning(Button.MinimumX + Button.Width() * 0.5f - 12.0f,
                        SymbolY,
                        24.0f,
                        24.0f), Colour.ColourFaint);
        const PlaneExtent CaptionClip = { Button.MinimumX + 6.0f,
                                          Button.MinimumY,
                                          Button.MaximumX - 6.0f,
                                          Button.MaximumY };
        Surface->Confine(CaptionClip);
        Surface->TextRunTruncated(Button.MinimumX + Button.Width() * 0.5f,
                                  Button.MaximumY - 27.0f,
                                  CaptionClip.MaximumX,
                                  Colour.ColourQuiet,
                                  SubjectTitle(Subjects[Index]),
                                  Measure.TextSmall,
                                  true);
        Surface->Release();

        if (Pressed(ResolveControlIndex(RecordIndex, Roles[Index]), Button))
            Discard(Partition.Assign(RecordIndex, Subjects[Index]));
    }

    Surface->Release();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DEFERRED MENUS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 EVERY MENU PLATE IS DRAWN THROUGH HERE. The GPU overlay pass records after the interface and
//    scissors to the whole viewport leaf, so a menu that does not report its box is textured over by the
//    grid and reads as transparent. Recording the box in the same call that fills it means a menu added
//    later cannot silently forget to, which is how the previous attempt at this fix was left inert.
void EditorPanel::RecordMenuPlate(const PlaneExtent& Menu, float Radius)
{
    const EditorPanelMetric& Measure = Appearance->EditorPanelMeasure;
    const EditorPanelColour& Colour  = Appearance->EditorPanel;

    Surface->Ground(Menu, Colour.ChromeGround, Radius, CornerAll);
    Surface->Edge(Menu, Colour.Edge, Measure.EdgeWeight, Radius, CornerAll);

    DisclosedExtent = Menu;
}

void EditorPanel::RecordDeferred(PanelStructure& Partition, EditorPanelConfiguration& Configuration)
{
    if (DeferredRecord >= PanelStructure::RecordLimit)
        return;

    const bool DividerPresent = DeferredDivider.Width() > 0.0f && DeferredDivider.Height() > 0.0f;
    if (DividerPresent && Pointer.ContactPressed &&
        !DeferredDivider.Encloses(Pointer.PositionX, Pointer.PositionY))
    {
        CloseDisclosure();
        return;
    }

    if (DividerPresent)
        Surface->Confine(DeferredDivider);

    switch (DeferredRole)
    {
        case ControlRole::SubjectMenu:
            RecordSubjectMenu(DeferredRecord, DeferredAnchor, Partition);
            break;
        case ControlRole::DivisionMenu:
            RecordDivisionMenu(DeferredRecord, DeferredAnchor, Partition);
            break;
        case ControlRole::LatticeMenu:
            RecordLatticeMenu(DeferredRecord, DeferredAnchor, Configuration);
            break;
        case ControlRole::CameraMenu:
        case ControlRole::OverlayMenu:
        case ControlRole::Shading:
        case ControlRole::Gizmo:
            RecordFooterMenu(DeferredRecord, DeferredAnchor, DeferredRole, Configuration);
            break;
        default:
            break;
    }

    // Component controls inside an editor popup (notably Grid Type) defer their own roster until
    // their parent card has been textured. Without this pass the field could disclose but no choices
    // were ever recorded.
    SharedControls.RecordDeferred();

    if (DividerPresent)
        Surface->Release();
}

void EditorPanel::RecordSubjectMenu(std::uint32_t RecordIndex,
                                    const PlaneExtent& Anchor,
                                    PanelStructure& Partition)
{
    const EditorPanelMetric& Measure = Appearance->EditorPanelMeasure;
    const EditorPanelColour&    Colour     = Appearance->EditorPanel;
    const float MenuX = (Measure.MenuX < DeferredDivider.Width())
                          ? Measure.MenuX : DeferredDivider.Width();
    const float DesiredMinimum = Anchor.MinimumX;
    const float MenuTop = (DesiredMinimum + MenuX > DeferredDivider.MaximumX)
                          ? DeferredDivider.MaximumX - MenuX
                          : (DesiredMinimum < DeferredDivider.MinimumX)
                          ? DeferredDivider.MinimumX : DesiredMinimum;
    const PlaneExtent Menu = Spanning(MenuTop,
                                      Anchor.MaximumY + Measure.MenuLift,
                                      MenuX,
                                      Measure.MenuPadY * 2.0f + Measure.MenuRowHeight * 6.0f);
    RecordMenuPlate(Menu, Measure.MenuRadius);

    // 📐 Ordered as the workspace reads: the two viewers, the scene tree, then
    //    the texture stack.
    // 🔴 Properties is NOT offered here. It records the Properties record
    //    inspector, which belongs to the shell's Scene Directory and not to an
    //    editor leaf; choosing it in the editor put a Properties pane inside a
    //    Texturing workspace. The subject and its host case remain so an
    //    existing layout that already holds one still draws, but it can no
    //    longer be newly chosen.
    const PanelSubject Subjects[6] = { PanelSubject::Viewport, PanelSubject::Uv,
                                       PanelSubject::Outliner, PanelSubject::SketchDirectory,
                                       PanelSubject::ParametricTools, PanelSubject::Texturing };
    const ControlRole Roles[6] = { ControlRole::ChooseViewport, ControlRole::ChooseUv,
                                   ControlRole::ChooseOutliner, ControlRole::ChooseSketchDirectory,
                                   ControlRole::ChooseParametricTools, ControlRole::ChooseTexturing };

    for (std::uint32_t Index = 0u; Index < 6u; ++Index)
    {
        const PlaneExtent Row = Spanning(Menu.MinimumX + Measure.MenuPadY,
                                         Menu.MinimumY + Measure.MenuPadY +
                                             static_cast<float>(Index) * Measure.MenuRowHeight,
                                         Menu.Width() - Measure.MenuPadY * 2.0f,
                                         Measure.MenuRowHeight);
        Symbol(Spanning(Row.MinimumX + 8.0f, Row.MinimumY + 7.0f,
                        Measure.HeaderSymbol, Measure.HeaderSymbol), Colour.ColourQuiet);
        Surface->TextRun(Row.MinimumX + 30.0f, Row.MinimumY + 7.0f,
                         Colour.ColourQuiet, SubjectTitle(Subjects[Index]), Measure.TextBody, 0.0f, false);
        if (Pressed(ResolveControlIndex(RecordIndex, Roles[Index]), Row, true))
        {
            Discard(Partition.Assign(RecordIndex, Subjects[Index]));
            CloseDisclosure();
        }
    }
}

void EditorPanel::RecordDivisionMenu(std::uint32_t RecordIndex,
                                     const PlaneExtent& Anchor,
                                     PanelStructure& Partition)
{
    const EditorPanelMetric& Measure = Appearance->EditorPanelMeasure;
    const EditorPanelColour&    Colour     = Appearance->EditorPanel;
    const float MenuX = (Measure.SplitMenuX < DeferredDivider.Width())
                          ? Measure.SplitMenuX : DeferredDivider.Width();
    const float DesiredMinimum = Anchor.MaximumX - MenuX;
    const float MenuTop = (DesiredMinimum + MenuX > DeferredDivider.MaximumX)
                          ? DeferredDivider.MaximumX - MenuX
                          : (DesiredMinimum < DeferredDivider.MinimumX)
                          ? DeferredDivider.MinimumX : DesiredMinimum;
    const PlaneExtent Menu = Spanning(MenuTop,
                                      Anchor.MaximumY + Measure.MenuLift,
                                      MenuX,
                                      Measure.MenuPadY * 2.0f + Measure.MenuRowHeight * 4.0f + 1.0f);
    RecordMenuPlate(Menu, Measure.MenuRadius);

    const char* Captions[4] = { "Split Left", "Split Right", "Split Top", "Split Bottom" };
    const ControlRole Roles[4] = { ControlRole::DivideLeft, ControlRole::DivideRight,
                                   ControlRole::DivideUpper, ControlRole::DivideLower };

    for (std::uint32_t Index = 0u; Index < 4u; ++Index)
    {
        const PlaneExtent Row = Spanning(Menu.MinimumX + Measure.MenuPadY,
                                         Menu.MinimumY + Measure.MenuPadY +
                                             static_cast<float>(Index) * Measure.MenuRowHeight,
                                         Menu.Width() - Measure.MenuPadY * 2.0f,
                                         Measure.MenuRowHeight);
        Symbol(Spanning(Row.MinimumX + 8.0f, Row.MinimumY + 7.0f,
                        Measure.HeaderSymbol, Measure.HeaderSymbol), Colour.ColourQuiet);
        Surface->TextRun(Row.MinimumX + 30.0f, Row.MinimumY + 7.0f,
                         Colour.ColourQuiet, Captions[Index], Measure.TextBody, 0.0f, false);
        if (Pressed(ResolveControlIndex(RecordIndex, Roles[Index]), Row, true))
        {
            const PanelDivisionAxis Axis = Index < 2u ? PanelDivisionAxis::X : PanelDivisionAxis::Y;
            const PanelDivisionSide Side = (Index == 0u || Index == 2u)
                                         ? PanelDivisionSide::Minimum : PanelDivisionSide::Maximum;
            Discard(Partition.Divide(RecordIndex, Axis, Side));
            CloseDisclosure();
        }
    }
}

void EditorPanel::RecordLatticeMenu(std::uint32_t RecordIndex,
                                 const PlaneExtent& Anchor,
                                 EditorPanelConfiguration& Configuration)
{
    const EditorPanelMetric& Measure = Appearance->EditorPanelMeasure;
    const EditorPanelColour&    Colour     = Appearance->EditorPanel;
    const float MenuX = (360.0f < DeferredDivider.Width()) ? 360.0f : DeferredDivider.Width();
    const float DesiredMinimum = Anchor.MinimumX;
    const float MenuTop = (DesiredMinimum + MenuX > DeferredDivider.MaximumX)
                          ? DeferredDivider.MaximumX - MenuX
                          : (DesiredMinimum < DeferredDivider.MinimumX)
                          ? DeferredDivider.MinimumX : DesiredMinimum;
    // 📐 The finite world extent and camera fade radius are ordinary shared magnitude rows.
    //    LatticeScale is retained for the skeletal lattice but the analytic ground reads the
    //    authored metre values directly.
    const float MenuHeight = 510.0f;
    const PlaneExtent Menu = Spanning(MenuTop,
                                      Anchor.MinimumY - (MenuHeight + 12.0f),
                                      MenuX,
                                      MenuHeight);
    if (Pointer.ContactPressed && !Menu.Encloses(Pointer.PositionX, Pointer.PositionY) &&
        !Anchor.Encloses(Pointer.PositionX, Pointer.PositionY))
    {
        CloseDisclosure();
        return;
    }
    RecordMenuPlate(Menu, 12.0f);
    Surface->TextRun(Menu.MinimumX + 20.0f, Menu.MinimumY + 18.0f,
                     Colour.ColourPrimary, "Grid settings", Measure.TextBody, 0.0f, false);
    Surface->Ground(Spanning(Menu.MinimumX + 20.0f, Menu.MinimumY + 44.0f,
                             Menu.Width() - 40.0f, 1.0f), Colour.Edge);

    // Deferred selection rendering borrows this roster after this function returns.
    static const char* const LatticeOptions[4] = { "None", "Lines", "Dotted", "Lines + Dots" };
    SelectionDeclaration LatticeDeclaration;
    LatticeDeclaration.Caption     = "Grid";
    LatticeDeclaration.Options     = LatticeOptions;
    LatticeDeclaration.OptionCount = 4u;
    std::uint32_t LatticeReading = static_cast<std::uint32_t>(Configuration.Lattice);
    SharedControls.SelectionField(Controls[ResolveControlIndex(RecordIndex, ControlRole::LatticePresentation)],
                                  Spanning(Menu.MinimumX + 20.0f, Menu.MinimumY + 58.0f,
                                           Menu.Width() - 40.0f, 36.0f),
                                  LatticeDeclaration,
                                  LatticeReading);
    Configuration.Lattice = static_cast<PanelLatticePresentation>(LatticeReading);

    const auto RowAt = [&](float FromTop) -> PlaneExtent
    {
        return Spanning(Menu.MinimumX + 20.0f, Menu.MinimumY + FromTop,
                        Menu.Width() - 40.0f, 36.0f);
    };

    MagnitudeDeclaration CellDeclaration;
    CellDeclaration.Caption   = "Minor cell";
    CellDeclaration.UnitGlyph = "m";
    CellDeclaration.Minimum   = 0.1;
    CellDeclaration.Maximum   = 100.0;
    CellDeclaration.Decimals  = 2u;
    CellDeclaration.Layout    = MagnitudeDeclaration::Arrange::Measured;
    SharedControls.MagnitudeRow(Controls[ResolveControlIndex(RecordIndex, ControlRole::LatticeCell)],
                                RowAt(106.0f), CellDeclaration,
                                Configuration.LatticeCellMetres, false);

    MagnitudeDeclaration ScaleDeclaration;
    ScaleDeclaration.Caption     = "Scale";
    ScaleDeclaration.UnitGlyph   = "x";
    ScaleDeclaration.Minimum= 1.0;
    ScaleDeclaration.Maximum = 10.0;
    ScaleDeclaration.Layout  = MagnitudeDeclaration::Arrange::Measured;
    double ScaleReading = static_cast<double>(Configuration.LatticeScale);
    SharedControls.MagnitudeRow(Controls[ResolveControlIndex(RecordIndex, ControlRole::LatticeScale)],
                                RowAt(150.0f), ScaleDeclaration, ScaleReading, false);
    Configuration.LatticeScale = static_cast<std::uint32_t>(std::round(ScaleReading));

    MagnitudeDeclaration SubdivisionDeclaration;
    SubdivisionDeclaration.Caption   = "Major step";
    SubdivisionDeclaration.UnitGlyph = "";
    SubdivisionDeclaration.Minimum   = 2.0;
    SubdivisionDeclaration.Maximum   = 64.0;
    SubdivisionDeclaration.Layout    = MagnitudeDeclaration::Arrange::Measured;
    double SubdivisionReading = static_cast<double>(Configuration.Subdivisions);
    SharedControls.MagnitudeRow(Controls[ResolveControlIndex(RecordIndex, ControlRole::Subdivisions)],
                                RowAt(194.0f), SubdivisionDeclaration, SubdivisionReading, false);
    Configuration.Subdivisions = static_cast<std::uint32_t>(std::round(SubdivisionReading));

    MagnitudeDeclaration WeightDeclaration;
    WeightDeclaration.Caption   = "Line weight";
    WeightDeclaration.UnitGlyph = "px";
    WeightDeclaration.Minimum   = 0.5;
    WeightDeclaration.Maximum   = 6.0;
    WeightDeclaration.Decimals  = 1u;
    WeightDeclaration.Layout    = MagnitudeDeclaration::Arrange::Measured;
    double WeightReading = static_cast<double>(Configuration.LatticeLineWeight);
    SharedControls.MagnitudeRow(Controls[ResolveControlIndex(RecordIndex, ControlRole::LatticeLineWeight)],
                                RowAt(238.0f), WeightDeclaration, WeightReading, false);
    Configuration.LatticeLineWeight = static_cast<float>(std::max(0.5, WeightReading));

    MagnitudeDeclaration DotDeclaration;
    DotDeclaration.Caption   = "Dot radius";
    DotDeclaration.UnitGlyph = "px";
    DotDeclaration.Minimum   = 1.0;
    DotDeclaration.Maximum   = 12.0;
    DotDeclaration.Decimals  = 1u;
    DotDeclaration.Layout    = MagnitudeDeclaration::Arrange::Measured;
    double DotReading = static_cast<double>(Configuration.LatticeDotRadius);
    SharedControls.MagnitudeRow(Controls[ResolveControlIndex(RecordIndex, ControlRole::LatticeDotRadius)],
                                RowAt(282.0f), DotDeclaration, DotReading, false);
    Configuration.LatticeDotRadius = static_cast<float>(std::max(1.0, DotReading));

    MagnitudeDeclaration ExtentDeclaration;
    ExtentDeclaration.Caption   = "World extent";
    ExtentDeclaration.UnitGlyph = "m";
    ExtentDeclaration.Minimum   = 1.0;
    ExtentDeclaration.Maximum   = 100000.0;
    ExtentDeclaration.Decimals  = 1u;
    ExtentDeclaration.Layout    = MagnitudeDeclaration::Arrange::Measured;
    SharedControls.MagnitudeRow(Controls[ResolveControlIndex(RecordIndex, ControlRole::LatticeExtent)],
                                RowAt(326.0f), ExtentDeclaration,
                                Configuration.LatticeExtentMetres, false);

    MagnitudeDeclaration FadeDeclaration;
    FadeDeclaration.Caption   = "Camera fade";
    FadeDeclaration.UnitGlyph = "m";
    FadeDeclaration.Minimum   = 1.0;
    FadeDeclaration.Maximum   = 100000.0;
    FadeDeclaration.Decimals  = 1u;
    FadeDeclaration.Layout    = MagnitudeDeclaration::Arrange::Measured;
    SharedControls.MagnitudeRow(Controls[ResolveControlIndex(RecordIndex, ControlRole::LatticeFadeRadius)],
                                RowAt(370.0f), FadeDeclaration,
                                Configuration.LatticeFadeRadiusMetres, false);

    ToggleDeclaration AxisDeclarations[3];
    AxisDeclarations[0].Caption = "X axis";
    AxisDeclarations[1].Caption = "Y axis";
    AxisDeclarations[2].Caption = "Z axis";
    bool* AxisReadings[3] = { &Configuration.AxisX, &Configuration.AxisY, &Configuration.AxisZ };
    const ControlRole AxisRoles[3] = { ControlRole::AxisX, ControlRole::AxisY, ControlRole::AxisZ };
    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        SharedControls.ToggleRow(Controls[ResolveControlIndex(RecordIndex, AxisRoles[Index])],
                                 Spanning(Menu.MinimumX + 20.0f + static_cast<float>(Index) * 106.0f,
                                          Menu.MinimumY + 424.0f,
                                          96.0f,
                                          44.0f),
                                 AxisDeclarations[Index],
                                 *AxisReadings[Index]);
    }
}

void EditorPanel::RecordFooterMenu(std::uint32_t RecordIndex,
                                   const PlaneExtent& Anchor,
                                   ControlRole Role,
                                   EditorPanelConfiguration& Configuration)
{
    const EditorPanelMetric& Measure = Appearance->EditorPanelMeasure;
    const EditorPanelColour&    Colour     = Appearance->EditorPanel;
    const auto Dismissed = [&](const PlaneExtent& Menu) -> bool
    {
        if (!Pointer.ContactPressed || Menu.Encloses(Pointer.PositionX, Pointer.PositionY) ||
            Anchor.Encloses(Pointer.PositionX, Pointer.PositionY))
            return false;
        CloseDisclosure();
        return true;
    };
    const auto FitExtent = [&](float DesiredMinimum,
                               float DesiredX,
                               float MinimumY,
                               float Height) -> PlaneExtent
    {
        const float Width = (DesiredX < DeferredDivider.Width())
                                ? DesiredX : DeferredDivider.Width();
        const float MinimumX = (DesiredMinimum + Width > DeferredDivider.MaximumX)
                               ? DeferredDivider.MaximumX - Width
                               : (DesiredMinimum < DeferredDivider.MinimumX)
                               ? DeferredDivider.MinimumX : DesiredMinimum;
        return Spanning(MinimumX, MinimumY, Width, Height);
    };

    if (Role == ControlRole::CameraMenu)
    {
        const PlaneExtent Menu = FitExtent(Anchor.MinimumX, 240.0f, Anchor.MinimumY - 116.0f, 104.0f);
        if (Dismissed(Menu)) return;
        RecordMenuPlate(Menu, 12.0f);
        Surface->TextRun(Menu.MinimumX + 12.0f, Menu.MinimumY + 14.0f,
                         Colour.ColourSecondary, "Saved Cameras", Measure.TextSmall, 0.0f, false);
        Surface->TextRun(Menu.MaximumX - 12.0f, Menu.MinimumY + 14.0f,
                         Colour.Accent, "+ Save", Measure.TextSmall, 0.0f, true);
        Surface->Ground(Spanning(Menu.MinimumX + 12.0f, Menu.MinimumY + 38.0f,
                                 Menu.Width() - 24.0f, 1.0f), Colour.Edge);
        Surface->TextRun(Menu.MinimumX + Menu.Width() * 0.5f, Menu.MinimumY + 70.0f,
                         Colour.ColourFaint, "No saved cameras", Measure.TextSmall, 0.0f, true);
        return;
    }

    if (Role == ControlRole::OverlayMenu)
    {
        const PlaneExtent Menu = FitExtent(Anchor.MaximumX - 200.0f,
                                           200.0f,
                                           Anchor.MinimumY - 132.0f,
                                           120.0f);
        if (Dismissed(Menu)) return;
        RecordMenuPlate(Menu, 12.0f);

        ToggleDeclaration Declarations[3];
        Declarations[0].Caption = "FPS Monitor";
        Declarations[1].Caption = "Storage Allocation";
        Declarations[2].Caption = "GPU Renderer";
        bool* Readings[3] = { &Configuration.FpsOverlay, &Configuration.StorageOverlay, &Configuration.RendererOverlay };
        const ControlRole Roles[3] = { ControlRole::AxisX, ControlRole::AxisY, ControlRole::AxisZ };
        for (std::uint32_t Index = 0u; Index < 3u; ++Index)
        {
            SharedControls.ToggleRow(Controls[ResolveControlIndex(RecordIndex, Roles[Index])],
                                     Spanning(Menu.MinimumX + 8.0f,
                                              Menu.MinimumY + 6.0f + static_cast<float>(Index) * 36.0f,
                                              Menu.Width() - 16.0f,
                                              34.0f),
                                     Declarations[Index],
                                     *Readings[Index]);
        }
        return;
    }

    const std::uint32_t OptionCount = Role == ControlRole::Shading ? 6u : 2u;
    const float MenuX = Role == ControlRole::Shading ? 160.0f : 130.0f;
    const PlaneExtent Menu = FitExtent(Anchor.MaximumX - MenuX,
                                       MenuX,
                                       Anchor.MinimumY - Measure.MenuPadY * 2.0f -
                                           Measure.MenuRowHeight * static_cast<float>(OptionCount) - 12.0f,
                                       Measure.MenuPadY * 2.0f +
                                           Measure.MenuRowHeight * static_cast<float>(OptionCount));
    if (Dismissed(Menu)) return;
    RecordMenuPlate(Menu, Measure.MenuRadius);

    const char* ShadingOptions[6] = { "solid", "wireframe", "matcap", "normal", "metallic", "gi" };
    const char* GizmoOptions[2] = { "blender", "cad" };
    const ControlRole OptionRoles[6] = { ControlRole::DivideLeft, ControlRole::DivideRight,
                                         ControlRole::DivideUpper, ControlRole::DivideLower,
                                         ControlRole::ChooseViewport, ControlRole::ChooseUv };
    for (std::uint32_t Index = 0u; Index < OptionCount; ++Index)
    {
        const PlaneExtent Row = Spanning(Menu.MinimumX + Measure.MenuPadY,
                                         Menu.MinimumY + Measure.MenuPadY +
                                             static_cast<float>(Index) * Measure.MenuRowHeight,
                                         Menu.Width() - Measure.MenuPadY * 2.0f,
                                         Measure.MenuRowHeight);
        const bool Taken = Role == ControlRole::Shading
                         ? static_cast<std::uint32_t>(Configuration.Shading) == Index
                         : static_cast<std::uint32_t>(Configuration.Gizmo) == Index;
        if (Taken)
            Surface->Ground(Row, Colour.Hovered, 4.0f, CornerAll);
        Surface->TextRun(Row.MinimumX + 12.0f, Row.MinimumY + 7.0f,
                         Taken ? Colour.ColourPrimary : Colour.ColourQuiet,
                         Role == ControlRole::Shading ? ShadingOptions[Index] : GizmoOptions[Index],
                         Measure.TextBody,
                         0.0f,
                         false);
        if (Pressed(ResolveControlIndex(RecordIndex, OptionRoles[Index]), Row, true))
        {
            if (Role == ControlRole::Shading)
                Configuration.Shading = static_cast<PanelShading>(Index);
            else
                Configuration.Gizmo = static_cast<PanelGizmo>(Index);
            CloseDisclosure();
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void EditorPanel::Reset()
{
    PropertyPresentation.Reset();
    OutlinerPresentation.Reset();
    UvPresentation.Reset();
    ScenePresentation.Reset();
    SharedControls.Reset();
    Interaction.Reset();
    Motion          = nullptr;
    Surface         = nullptr;
    Appearance      = nullptr;
    Pointer                = {};
    CurrentLeafExtent      = {};
    DeferredAnchor         = {};
    DeferredDivider       = {};
    DeferredRecord         = PanelStructure::RecordLimit;
    DeferredRole           = ControlRole::RoleCount;
    CurrentPresentation    = 0u;
    CapturedPresentation   = AbsentPresentation;
    DisclosedPresentation  = AbsentPresentation;
    DraggedDivision        = PanelStructure::RecordLimit;
    DraggedExtent          = {};

    for (std::uint32_t Index = 0u; Index < ControlCapacity; ++Index)
        Controls[Index] = {};
}

}   // namespace Slate
