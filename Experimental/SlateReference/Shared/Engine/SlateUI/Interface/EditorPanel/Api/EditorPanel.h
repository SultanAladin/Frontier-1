//============================================================================================================================================
//                                                            EDITORPANEL.H
//============================================================================================================================================
// 🧩 Reusable editor-panel chrome, split interaction and skeletal viewport, UV, outliner and property presentations.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/ComponentSpecification/Api/ComponentSpecification.h"
#include "SlateUI/Interface/EditorPanel/Api/EditorLeafPanels.h"
#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"
#include "SlateUI/Interface/PanelStructure/Api/PanelStructure.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    HOST-OWNED ORDINATES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Lattice presentation selected for viewport and UV panels.
/// tag   guarantee
enum class PanelLatticePresentation : std::uint32_t
{
    None              = 0u,
    Lines             = 1u,
    Dots              = 2u,
    LinesAndDots      = 3u,
    PresentationCount = 4u
};

/// 🧩 Scene shading selected for a viewport panel.
/// tag   guarantee
enum class PanelShading : std::uint32_t
{
    Lit                = 0u,
    Matcap             = 1u,
    SourceWire         = 2u,
    TriangulatedWire   = 3u,
    Points             = 4u,
    Normal             = 5u,
    Metallic           = 6u,
    Illumination       = 7u,
    ShadingCount       = 8u,

    Solid              = Lit,
    Wireframe          = TriangulatedWire
};

/// 🧩 Gizmo convention selected for a viewport panel.
/// tag   guarantee
enum class PanelGizmo : std::uint32_t
{
    Blender    = 0u,
    Cad        = 1u,
    GizmoCount = 2u
};

enum class EditorFooterDemand : std::uint32_t
{
    None = 0u,
    SceneImport,
    SceneExport,
    ExportFlattened,
    LayerExport
};

/// 🧩 Visible preferences and one-tick modular footer requests retained by the host.
/// tag   guarantee, nonallocating, nonthrowing
struct EditorPanelConfiguration
{
    PanelLatticePresentation  Lattice         = PanelLatticePresentation::Lines;   // [-] - lattice presentation
    PanelShading              Shading         = PanelShading::Lit;                  // [-] - viewport shading
    PanelGizmo                Gizmo           = PanelGizmo::Blender;                // [-] - gizmo convention
    std::uint32_t             LatticeScale    = 1u;                                 // [-] - skeletal lattice scale
    std::uint32_t             Subdivisions    = 10u;                                // [-] - major line every N cells
    bool                      AxisX           = true;                               // [-] - X or U axis visible
    bool                      AxisY           = true;                               // [-] - Y or V axis visible
    bool                      AxisZ           = true;                               // [-] - Z axis visible
    bool                      Perspective     = true;                               // [-] - perspective projection
    bool                      FpsOverlay      = false;                              // [-] - FPS overlay requested
    bool                      StorageOverlay  = false;                              // [-] - storage overlay requested
    bool                      RendererOverlay = false;                              // [-] - renderer overlay requested
    // 📝 The extended lattice surface the footer popup edits. Extent limits the grid around world
    //    centre; fade radius independently limits what remains sharp around the moving camera. The
    //    fragment stage combines both, so the analytic grid remains finite without CPU tessellation.
    double                    LatticeCellMetres = 1.0;                              // [m] - one lattice cell
    float                     LatticeLineWeight  = 1.0f;                            // [px] - fine line thickness
    float                     LatticeDotRadius   = 2.0f;                            // [px] - fine dot radius
    double                    LatticeExtentMetres = 100.0;                          // [m] - radius from world centre
    double                    LatticeFadeRadiusMetres = 40.0;                       // [m] - sharp-to-absent camera radius
    EditorFooterDemand        FooterDemand = EditorFooterDemand::None;               // [-] - one-tick panel action
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        PRESENTATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Presents one host-owned workspace partition and edits it through exact panel chrome controls.
/// note  GPU content is deliberately absent: viewport and UV bodies are render-target placeholders while
///       panel selection, binary division, resizing, withdrawal and reusable footer controls are functional.
/// tag   owning, nonallocating, nonthrowing
class EditorPanel
{
public:

    static constexpr std::uint32_t ControlsPerRecord = 30u;
    static constexpr std::uint32_t ControlCapacity = PanelStructure::RecordLimit * ControlsPerRecord;

    Deliver<bool> ConstructEditorPanel(MotionIntegrator& Motion,
                            RecordingSurface& Surface,
                            const ThemeProfile& Appearance);
    void Advance(const PointerCondition& Sampled, double Elapsed);
    Deliver<bool> Record(const PlaneExtent& Extent,
                         PanelStructure& Partition,
                         EditorPanelConfiguration& Configuration,
                         std::uint32_t PresentationIndex = 0u,
                         bool DeferPopups = false);
    bool PointerCaptured(std::uint32_t PresentationIndex) const;
    void WithdrawPresentation(std::uint32_t PresentationIndex);
    void Reset();

    /// 🧩 How many leaves the last `Record` presented, for the host to fill their bodies with content.
    /// note  ⚠️ Valid only until the next `Record`. Leaves are the partition's occupied, non-vacant slots,
    ///        in depth-first order; a vacant panel is a chooser and carries no content of its own.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t LeafCount() const { return LeafTally; }

    /// 🧩 The body of one leaf the last `Record` left, between its header and its footer.
    /// note  ⚠️ Valid only until the next `Record`, and empty when `LeafIndex` is out of range. The host
    ///        records its content — the sky in a viewport leaf, the outliner, the properties — into this
    ///        extent, on top of the leaf's own ground and caption, before the footer is drawn over it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    PlaneExtent LeafBody(std::uint32_t LeafIndex) const
    {
        return LeafIndex < LeafTally ? LeafBodies[LeafIndex] : PlaneExtent{};
    }

    /// 🧩 What one leaf presents, so the host decides which content to record into its body.
    /// note  ⚠️ Valid only until the next `Record`, and `Vacant` when `LeafIndex` is out of range.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    PanelSubject LeafSubject(std::uint32_t LeafIndex) const
    {
        return LeafIndex < LeafTally ? LeafSubjects[LeafIndex] : PanelSubject::Vacant;
    }

    /// 🧩 Records the deferred popups (subject, division, lattice, shading and gizmo menus) the last
    ///    `Record` withheld when `DeferPopups` was declared.
    /// note  🔴 The host calls this AFTER recording its leaf content: a popup recorded before the leaf
    ///        content is textured over by the sky quad and the split menus become unreadable — which was
    ///        the reported defect. Other hosts that never fill the leaves keep `Record`'s default and
    ///        never call this.
    /// note  ⚠️ Valid only between the `Record` that deferred and the next one.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    void RecordDeferredPopups(PanelStructure& Partition, EditorPanelConfiguration& Configuration);

    /// 🧩 Whether any popup (subject, division, lattice, shading or gizmo menu) stands right now.
    /// note  🔴 The GPU overlay pass records AFTER the interface, so an open popup would be textured
    ///        over by the grid and the axes — the reported "the lines draw on the menus". The host
    ///        tests this and withholds the leaf overlays while a popup stands.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool AnyPopupStanding() const
    {
        return DisclosedPresentation != AbsentPresentation;
    }

    /// 🧩 The box the standing popup last recorded, in display ordinates.
    /// out   Result  [px] a zero-area extent when no popup stands, or none has been recorded yet
    /// note  🔴 THE GRID DREW THROUGH THE OPEN MENUS. The GPU overlay pass records AFTER the interface
    ///        and its scissor is the WHOLE viewport leaf, so the grid, the axes and the sketch all textured
    ///        straight over any menu opened from the footer -- which reads as a transparent dropdown,
    ///        because what shows through is exactly the viewport behind it. The colours were opaque the
    ///        whole time. The host subtracts this box from the overlay scissor instead of suppressing the
    ///        overlay outright: a menu is small and blanking a whole viewport's grid to protect it trades
    ///        one defect for a worse one.
    /// note  ⚠️ Valid only after the `RecordDeferredPopups` that drew it, and until the next `Record`.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const PlaneExtent& PopupExtent() const { return DisclosedExtent; }

private:

    static constexpr std::uint32_t AbsentPresentation = 0xFFFFFFFFu;

    enum class ControlRole : std::uint32_t
    {
        SubjectMenu = 0u,
        DivisionMenu,
        Withdrawal,
        DivideLeft,
        DivideRight,
        DivideUpper,
        DivideLower,
        ChooseViewport,
        ChooseUv,
        ChooseOutliner,
        // 🔴 Retained though no chooser now spends it: ControlRole ordinals are
        //    the control identities, so deleting one renumbers every role after
        //    it and moves the hover and contact state of unrelated controls.
        ChooseProperties,
        ChooseTexturing,
        ChooseParametricTools,
        ChooseSketchDirectory,
        LatticeMenu,
        CameraMenu,
        OverlayMenu,
        LatticePresentation,
        LatticeScale,
        Subdivisions,
        LatticeCell,
        LatticeLineWeight,
        LatticeDotRadius,
        LatticeExtent,
        LatticeFadeRadius,
        AxisX,
        AxisY,
        AxisZ,
        Shading,
        Gizmo,
        RoleCount
    };

    std::uint32_t ResolveControlIndex(std::uint32_t RecordIndex, ControlRole Role) const;
    bool Pressed(std::uint32_t ControlIndex, const PlaneExtent& Extent, bool PopupAction = false);
    bool Disclosed(ControlIdentity Target) const;
    void Disclose(ControlIdentity Target);
    void CloseDisclosure();
    void RecordBranch(std::uint32_t RecordIndex,
                      const PlaneExtent& Extent,
                      PanelStructure& Partition,
                      EditorPanelConfiguration& Configuration);
    void RecordLeaf(std::uint32_t RecordIndex,
                    const PanelRecord& Declared,
                    const PlaneExtent& Extent,
                    PanelStructure& Partition,
                    EditorPanelConfiguration& Configuration);
    void RecordHeader(std::uint32_t RecordIndex,
                      PanelSubject Subject,
                      const PlaneExtent& Extent,
                      PanelStructure& Partition);
    void RecordFooter(std::uint32_t RecordIndex,
                      PanelSubject Subject,
                      const PlaneExtent& Extent,
                      EditorPanelConfiguration& Configuration);
    void RecordVacant(std::uint32_t RecordIndex,
                      const PlaneExtent& Extent,
                      PanelStructure& Partition);
    void RecordDeferred(PanelStructure& Partition, EditorPanelConfiguration& Configuration);
    void RecordSubjectMenu(std::uint32_t RecordIndex,
                           const PlaneExtent& Anchor,
                           PanelStructure& Partition);
    void RecordDivisionMenu(std::uint32_t RecordIndex,
                            const PlaneExtent& Anchor,
                            PanelStructure& Partition);
    void RecordLatticeMenu(std::uint32_t RecordIndex,
                           const PlaneExtent& Anchor,
                           EditorPanelConfiguration& Configuration);
    void RecordFooterMenu(std::uint32_t RecordIndex,
                          const PlaneExtent& Anchor,
                          ControlRole Role,
                          EditorPanelConfiguration& Configuration);
    void RecordMenuPlate(const PlaneExtent& Menu, float Radius);
    void Symbol(const PlaneExtent& Extent, ThemeToken Colour);

    MotionIntegrator* Motion = nullptr;
    RecordingSurface* Surface = nullptr;
    const ThemeProfile* Appearance = nullptr;
    ControlIndex Interaction = {};
    ComponentSpecification SharedControls = {};
    LeafPanel ScenePresentation = {};
    LeafPanel UvPresentation = {};
    LeafPanel OutlinerPresentation = {};
    LeafPanel PropertyPresentation = {};
    LeafPanel LayerStackPresentation = {};
    ControlIdentity Controls[ControlCapacity] = {};
    PointerCondition Pointer = {};
    PlaneExtent CurrentLeafExtent = {};
    PlaneExtent DeferredAnchor = {};
    PlaneExtent DeferredDivider = {};
    std::uint32_t DeferredRecord = PanelStructure::RecordLimit;
    ControlRole DeferredRole = ControlRole::RoleCount;
    std::uint32_t CurrentPresentation = 0u;
    std::uint32_t CapturedPresentation = AbsentPresentation;
    std::uint32_t DisclosedPresentation = AbsentPresentation;
    PlaneExtent   DisclosedExtent       = {};   // [px] - the standing popup's box, for the overlay scissor
    std::uint32_t DraggedDivision = PanelStructure::RecordLimit;
    PlaneExtent DraggedExtent = {};

    // 📝 The leaves the last `Record` presented, for the host to fill. `LeafTally` resets at the top
    //    of every `Record`; a vacant leaf is a chooser and is not counted.
    PlaneExtent LeafBodies[PanelStructure::RecordLimit] = {};
    PanelSubject LeafSubjects[PanelStructure::RecordLimit] = {};
    std::uint32_t LeafTally = 0u;
};

}   // namespace Slate
