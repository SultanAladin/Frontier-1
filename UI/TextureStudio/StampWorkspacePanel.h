/*============================================================================================================================================
                                                          STAMPWORKSPACEPANEL.H
============================================================================================================================================*/
// 🧩 Native Stamp (decal) workspace presentation and gestures: the same toolbar/canvas feel as Pattern, a simpler
//    single-decal purpose — Text/Vector/Image components over a bounded 24-layer stack, no pattern tiling.

#pragma once
#include "Engine/Interface/VectorAuthoringControls.h"
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/ContentInterchange/VectorAuthoringDocument.h"
#include "Engine/ContentInterchange/VectorAuthoringSelectionEditing.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include "Engine/Lifecycle/ActivationSequence.h"

namespace Frontier
{
// 📝 Measures Content's natural glyph-outline bounds at a reference point size and returns the
// point size that makes it fill {BoxWidth, BoxHeight} as closely as possible without distorting the
// font (both axes scaled uniformly, so the box's narrower-relative axis governs) — the "fit to box"
// behaviour an SVG/Illustrator-style text tool gives when you drag out a frame and type into it.
// Exposed (not file-local) so both the live on-canvas composer preview and the final bake-on-commit
// call the exact same real function, and so a headless CPU test can exercise it directly.
[[nodiscard]] float MeasureStampTextFitSize(const std::string &Content, float BoxWidth, float BoxHeight, const std::filesystem::path &ContentRoot);
class StampWorkspacePanel final : public ActivationSequence
{
public:
    StampWorkspacePanel();
    void ConstructInterface();
    void ConfigureInterfaceScale(float Scale);
    void ConfigureTypeface(void *Regular, void *Light = nullptr);
    // 📝 AppendStampTextOutline reads a real TrueType file off disk (for glyph-outline geometry,
    // distinct from the ImGui chrome typefaces above) via a path that defaults to a bare relative
    // literal; that only resolves when the process's current working directory happens to be the
    // EngineContent-containing folder. The host app already resolves its real executable-relative
    // content root once at startup — hand it here so "Add text..." keeps working regardless of how
    // the packaged binary was launched (shortcut, different "Start in" folder, etc).
    void ConfigureContentRoot(std::filesystem::path Root) { ContentRoot = std::move(Root); }
    void ConfigureDocument(VectorAuthoringDocument Document);
    // 📝 One small GPU atlas carries the open Stamp's own layer row-thumbnails (the Texture-workspace-style
    // preview inside each layer row), rebaked whenever the document edits — mirrors Pattern's layer atlas,
    // Stamp just has no library-wide gallery atlas since there is no preset library to browse.
    static constexpr uint32_t LayerAtlasTile = 64;
    static constexpr uint32_t LayerAtlasColumns = 12;
    static constexpr uint32_t LayerAtlasExtent = (LayerAtlasTile + 2) * LayerAtlasColumns;
    static constexpr size_t LayerAtlasCapacity = size_t(LayerAtlasColumns) * LayerAtlasColumns;
    struct StampAtlasImage
    {
        uint64_t Texture = 0;
        float MinimumU = 0, MinimumV = 0, MaximumU = 1, MaximumV = 1;
    };
    std::function<StampAtlasImage()> QueryLayerThumbnailAtlas;
    [[nodiscard]] uint64_t QueryLayerAtlasRevision();
    [[nodiscard]] const std::vector<uint8_t> &QueryLayerAtlasPixels();
    [[nodiscard]] static uint32_t QueryLayerAtlasExtent() noexcept { return LayerAtlasExtent; }
    [[nodiscard]] const VectorAuthoringDocument &QueryDocument() const;
    [[nodiscard]] const std::vector<uint32_t> &QuerySelection() const noexcept { return Selection; }
    [[nodiscard]] uint64_t QueryRevision() const noexcept { return History.QueryRevision(); }
    [[nodiscard]] PanelDockspace &QueryPanels() noexcept { return Panels; }
    [[nodiscard]] PlaneExtent QueryControlExtent(const std::string &Caption) const;
    [[nodiscard]] PlaneExtent QueryPanelExtent(const std::string &Caption) const;
    [[nodiscard]] PlaneExtent QueryBoardExtent() const noexcept { return BoardExtent; }
    [[nodiscard]] bool QueryModified() const { return QueryDocument() != SavedDocument; }
    [[nodiscard]] bool QueryAuthoringSettled() const noexcept { return Gesture == 0; }
    [[nodiscard]] const std::string &QueryDiagnostic() const noexcept { return Diagnostic; }
    bool RequestClose();
    bool ConsumeMaterialApplyRequest() noexcept;
    // 📝 Second argument is the device-pixel edge the returned image will actually be displayed at
    // (the zoomed board, framebuffer-scaled): lets the GPU bake at that resolution instead of a
    // fixed-size raster stretched by the zoom transform.
    std::function<uint64_t(const VectorCoverageProgram &, uint32_t)> QueryCoverage;
    std::function<bool()> QueryCoverageReady;
    std::function<std::optional<uint32_t>(const VectorAuthoringDocument &, PlanePoint)> QuerySelectionToken;
    std::function<bool(std::string_view)> SaveSource;
    std::function<std::optional<std::string>()> LoadSource;
    std::function<void()> ActivateMaterial;
    std::function<void()> ActivatePattern;
    std::function<void()> CancelClose;
    std::function<PlanePoint(PlanePoint)> EvaluateNavigationDelta;
private:
    // 📝 Text is appended last, not inserted among Diamond..Star: Gesture==2's primitive math does
    // `uint32_t(Tool) - uint32_t(Diamond)` to pick a shape topology, which relies on Diamond..Star
    // staying contiguous. Text never reaches that arithmetic (it takes its own branch).
    enum class VectorAuthoringTool { Select, Rectangle, Ellipse, Diamond, Triangle, Polygon, Star, Pan, Text };
    // 📝 Constructed with Stamp=true: StampWorkspacePanel's own constructor immediately seeds a
    // Stamp document (Blank.Stamp = true) via ConfigureDocument -> History.Reset, which enforces
    // Replacement.Stamp == the history's own workspace contract. Left defaulted to Stamp=false,
    // that first Reset throws on every launch, before the window is usable.
    VectorAuthoringHistory History{true};
    VectorAuthoringDocument SavedDocument;
    VectorCoverageProgram CachedCoverageProgram;
    uint32_t CachedCoverageMask = UINT32_MAX;
    uint64_t CachedHistoryRevision = UINT64_MAX;
    uint64_t CachedGestureRevision = UINT64_MAX;
    uint64_t GestureRevision = 0;
    uint32_t GesturePrimitiveToken = 0;
    bool CachedCoverageValid = false;
    // 📝 Mirrors Pattern's own bake-badge debounce: only surface "Baking…" once a dispatch has
    // stayed pending past this threshold, instead of flashing it on every single edit.
    double PendingBakeSince = -1;
    bool ApplyToMaterialRequested = false;
    std::optional<VectorAuthoringDocument> SelectionClipboard;
    void TransferSelection(bool Remove);
    void InsertSelection(bool InPlace);
    void ReplicateSelection();
    void SelectEditableVectors();

    std::vector<uint32_t> Selection, GestureSelection;
    std::vector<std::vector<uint32_t>> UndoSelections, RedoSelections;
    PanelDockspace Panels;
    VectorAuthoringControlSurface Controls;
    std::map<std::string, PlaneExtent> ControlExtents, PanelExtents;
    uint64_t LayerAtlasRevision = 0;
    uint64_t LayerAtlasBakedRevision = UINT64_MAX;
    std::vector<uint8_t> LayerAtlasPixels;
    // 📝 Mirrors Pattern's own per-layer thumbnail cache exactly: reuse a component's last raster
    // whenever its own cropped SVG text is unchanged, so one edited component does not force a
    // ThorVG parse+raster pass over every other component in the decal on the UI thread.
    std::map<uint32_t, std::pair<std::string, std::vector<uint8_t>>> LayerThumbnailCache;
    std::filesystem::path ContentRoot;
    void RefreshLayerAtlas();
    std::optional<VectorAuthoringDocument> GestureSource, GestureDraft;
    VectorAuthoringTool Tool = VectorAuthoringTool::Select;
    PlaneExtent BoardExtent;
    PlanePoint PointerOrigin{}, Pan{}, PanOrigin{}, SelectionPointer{};
    float SelectionTravel = 0;
    bool SelectionReleased = false;
    float Scale = 1, Zoom = 1;
    int Gesture = 0, Filter = 0;
    std::array<int, 2> ResizeDirection{};
    bool MarqueeAdditive = false, SelectionExtend = false;
    uint32_t MaskSelection = 0;
    bool ChannelsOpen = false, CloseRequested = false;
    bool TransparentCanvas = false, GridEnabled = false, SnappingEnabled = false;
    void *RegularTypeface = nullptr, *LightTypeface = nullptr;
    std::array<char, 81> NameText{};
    std::array<char, 129> SearchText{};
    std::string Diagnostic;
    // 📝 Add-component popups: Vector/Image share one small staging buffer set rather than the
    // library-gallery machinery Pattern uses, since Stamp has no preset library to browse — only a
    // bounded 24-layer stack of hand-authored or imported decal components. Text has its own
    // draw-a-box-then-write-on-canvas flow (see TextEditBounds/ConstructTextComposer) instead of a
    // modal, but still stages into ComponentTextBuffer/ComponentEditToken below.
    std::array<char, 2049> ComponentTextBuffer{};
    std::string ComponentSvgBuffer;
    std::string ComponentImageBuffer;
    uint32_t ComponentEditToken = 0; // 0 creates a new component; nonzero replaces that layer's content in place.
    // 📝 ImGui popup IDs are seeded by the ID stack of whatever window is current when OpenPopup()
    // runs. "Add vector/image..." are selected from deep inside the "Add a stamp component"
    // dropdown's own transient popup window, so an OpenPopup() called right there would be keyed to
    // that window and never match the BeginPopupModal() polled later from the stable Layers panel
    // scope — the modal would silently never appear. Stash the request and open it from that stable
    // scope instead, alongside the matching BeginPopupModal calls.
    const char *PendingComponentPopup = nullptr;
    // 📝 The SVG-text-tool-style composer: Gesture 8 means "an on-canvas text field is live". The
    // user either just dragged (or default-placed) a box with the Text tool, or double-clicked /
    // chose "Edit on canvas..." on an existing text component. TextEditBounds is {centerX, centerY,
    // width, height} in the 512-unit document plane; TextBoxCurrent tracks the live drag endpoint
    // while the box itself is still being drawn (Gesture 2, Tool == Text, before Gesture 8 starts).
    std::array<float, 4> TextEditBounds{};
    PlanePoint TextBoxCurrent{};
    bool TextEditFocusPending = false;
    // 📝 Live auto-fit cache for the on-canvas composer (see ConstructTextComposer): recomputed only
    // when the composed text actually changes (never every frame — MeasureStampTextFitSize rebuilds
    // real glyph outlines), so the font size shown while typing already matches the size the commit
    // will bake at, instead of typing at a small generic caret size and only jumping to the real,
    // much larger auto-fit size once the box commits.
    std::string TextComposerFitCacheKey;
    float TextComposerFitPointSize = 0;
    std::array<char, 2049> InspectorTextBuffer{};
    uint32_t InspectorTextToken = 0;
    void ConstructAddComponentMenu();
    void ConstructTextComposer(float BoardMinimumX, float BoardMinimumY, float BoardSize);
    void CommitTextComposer();
    void RebakeTextLayer(uint32_t Token, const std::string &Content, float PointSize, float WrapWidth = 0.f);
    void ConstructVectorPopup();
    void ConstructImagePopup();
    void ConstructHeader();
    void ConstructLayers();
    void ConstructCanvas();
    void ConstructInspector();
    void ConstructCloseDialogue();
    void ConstructPrimitive(uint32_t Category, const char *Caption);
    void Finalize(VectorAuthoringDocument Document, std::vector<uint32_t> Tokens);
    void EvaluateEdit(const std::function<void(VectorAuthoringDocument &)> &Edit);
    void RestoreHistory(bool Redo);
    void Select(uint32_t Token, bool Extend = false);
    void CapturePanel(const char *Caption);
    void ApplySourcePatch(const VectorAuthoringSourcePatch &Patch);
    bool Save();
    void SuspendContent() noexcept override;
    void FinalizeContent() noexcept override;
};
}
