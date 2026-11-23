//============================================================================================================================================
//                                                          INTERFACEEXCHANGE.CPP
//============================================================================================================================================
// 🧩 The only translation unit in the engine that includes ImGui.

#include "SlateUI/Interface/InterfaceExchange/Api/InterfaceExchange.h"

#include "imgui.h"

// 📝 🔴 The internal header, for `ImGuiWindow::DockNode` alone. `DockNode` and `SelectedTabId` are the only
//    way to ask whether a DOCKED workspace is the one its node is showing, and the public header exposes
//    no equivalent. Included here and nowhere else — `00` §2.2's one-copy rule is about the library, and
//    this translation unit is already the single place ImGui is spelled.
#include "imgui_internal.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

// 📝 The cursor warp in `Seal()` names GLFW directly — the one window-system call the seam needs
//    beyond the backend, which exposes no warp API of its own.
#include <GLFW/glfw3.h>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  DESCRIPTOR EXTENT
//------------------------------------------------------------------------------------------------------------------------

// 📝 The interface's own descriptor extent, sized here rather than shared with `06`'s `DescriptorIndex`.
//    The interface allocates for its own imagery and for nothing else, so a shared extent would couple two
//    lifetimes that are reclaimed at different moments.
namespace
{
    // 📝 ImGui 19281 replaced VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER with two separate
    //    descriptor types. The pool must carry both:
    //      - SAMPLED_IMAGE  : one set per texture registered via ImGui_ImplVulkan_AddTexture().
    //      - SAMPLER        : one set per built-in sampler (linear + nearest = 2 minimum).
    //    InterfaceSamplerCapacity matches IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE (2)
    //    without pulling imgui_impl_vulkan.h into this translation unit.
    constexpr std::uint32_t InterfaceDescriptorCapacity = 64u;   // [-] sampled-image sets
    constexpr std::uint32_t InterfaceSamplerCapacity    = 2u;    // [-] sampler sets (linear + nearest)
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

InterfaceExchange::~InterfaceExchange()
{
    Reclaim();
}

Deliver<bool> InterfaceExchange::AttachInterface(const InterfaceAttachment& Incoming)
{
    if (ContextSlot != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the interface context already exists" });

    if (Incoming.Instance         == VK_NULL_HANDLE ||
        Incoming.ScoredDevice     == VK_NULL_HANDLE ||
        Incoming.ActiveDevice     == VK_NULL_HANDLE ||
        Incoming.GraphicsQueue    == VK_NULL_HANDLE ||
        Incoming.NativeWindowSlot == nullptr)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::CapabilityAbsent, "a required device or window handle was absent" });
    }

    if (Incoming.ColourTargetFormat == VK_FORMAT_UNDEFINED)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::CapabilityAbsent, "no colour target format was declared" });
    }

    if (Incoming.MinimumDisplayImageCount < 2u ||
        Incoming.DisplayImageCount < Incoming.MinimumDisplayImageCount)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the display image counts are inconsistent" });
    }

    Attached = Incoming;

    // 📝 ImGui 19281 allocates SAMPLED_IMAGE sets for textures and SAMPLER sets for its two
    //    built-in samplers. A pool that carries only COMBINED_IMAGE_SAMPLER has neither type and
    //    the first allocation fails with VK_ERROR_OUT_OF_POOL_MEMORY at validation time.
    const VkDescriptorPoolSize DescriptorExtent[] =
    {
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, InterfaceDescriptorCapacity },
        { VK_DESCRIPTOR_TYPE_SAMPLER,       InterfaceSamplerCapacity    },
    };

    VkDescriptorPoolCreateInfo DescriptorDeclaration = {};
    DescriptorDeclaration.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    DescriptorDeclaration.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    DescriptorDeclaration.maxSets       = InterfaceDescriptorCapacity + InterfaceSamplerCapacity;
    DescriptorDeclaration.poolSizeCount = 2u;
    DescriptorDeclaration.pPoolSizes    = DescriptorExtent;

    if (vkCreateDescriptorPool(Attached.ActiveDevice, &DescriptorDeclaration, nullptr, &DescriptorSlot) != VK_SUCCESS)
    {
        Attached = {};
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the interface descriptor extent was rejected" });
    }

    IMGUI_CHECKVERSION();
    ImGuiContext* ConstructedContext = ImGui::CreateContext();

    if (ConstructedContext == nullptr)
    {
        vkDestroyDescriptorPool(Attached.ActiveDevice, DescriptorSlot, nullptr);
        DescriptorSlot = VK_NULL_HANDLE;
        Attached       = {};
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the interface context was not constructed" });
    }

    ContextSlot = static_cast<void*>(ConstructedContext);

    ImGui::StyleColorsDark();

    // 🔴 Docking, enabled here and nowhere else. The submodule stands on ImGui's `docking` branch and the
    //    whole feature is inert until this flag is raised — the branch carries the code, not the behaviour.
    // 📝 ⚠️ Multi-viewport is deliberately NOT raised. It hands panels to the window manager as real OS
    //    windows, each with its own swapchain, and every swapchain in this process belongs to
    //    `DisplayScheduler` against one surface. Raising it would stand a second, unowned chain outside
    //    `HostLifecycle`'s five lifetimes and outside its resize and rebuild paths both.
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // 🔴 A WORKSPACE COULD BE DRAGGED FROM ANYWHERE INSIDE IT. ImGui moves a window
    //    when the press lands on any part of it that no widget claimed, and a Slate
    //    leaf is drawn almost entirely through the recording surface — the vendor
    //    sees empty client area under nearly all of it. So a press on a panel's
    //    body, between two rows, or on a card's ground would pick the whole
    //    workspace up and carry it, which reads as the layout coming apart under
    //    the pointer.
    //
    //    `ConfigWindowsMoveFromTitleBarOnly` confines the move to the title bar —
    //    and, for a docked window, to its TAB, which is the workspace strip at the
    //    top. That is exactly the requirement: the header that houses the
    //    workspaces drags; nothing in the body does.
    //
    //    📝 This is a configuration bit rather than a per-window `NoMove` because
    //    `NoMove` on the workspace window would also forbid tearing a workspace out
    //    by its tab, which is the one drag that must keep working.
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;

    // 📝 The window system attachment installs its own callbacks. `04`'s `InputExchange` keeps its arrival
    //    stamps regardless: the interface reads the accumulated window condition, and the stroke path reads
    //    the stamped arrival ordering. They observe the same device through two surfaces that never merge.
    if (!ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(Attached.NativeWindowSlot), true))
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the window system attachment rejected" });
    }

    // 📝 The window handle is retained for the look gesture's cursor warp: the warp must run AFTER the
    //    frame ends (the seam's Seal), never through the backend's WantSetMousePos, which fires at the
    //    start of the NEXT backend NewFrame and clobbers io.MousePos to the centre before NewFrame
    //    computes io.MouseDelta — the reported "the camera turns once and then stops" (actually: never
    //    turns while held) defect.
    NativeWindow = Attached.NativeWindowSlot;
    WindowAttached = true;

    VkFormat DeclaredColourFormat = Attached.ColourTargetFormat;

    VkPipelineRenderingCreateInfoKHR RecordingDeclaration = {};
    RecordingDeclaration.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    RecordingDeclaration.colorAttachmentCount    = 1u;
    RecordingDeclaration.pColorAttachmentFormats = &DeclaredColourFormat;

    ImGui_ImplVulkan_InitInfo VendorAttachment = {};
    VendorAttachment.ApiVersion                                     = VK_API_VERSION_1_3;
    VendorAttachment.Instance                                       = Attached.Instance;
    VendorAttachment.PhysicalDevice                                 = Attached.ScoredDevice;
    VendorAttachment.Device                                         = Attached.ActiveDevice;
    VendorAttachment.QueueFamily                                    = Attached.GraphicsFamilyIndex;
    VendorAttachment.Queue                                          = Attached.GraphicsQueue;
    VendorAttachment.DescriptorPool                                 = DescriptorSlot;
    VendorAttachment.MinImageCount                                  = Attached.MinimumDisplayImageCount;
    VendorAttachment.ImageCount                                     = Attached.DisplayImageCount;
    VendorAttachment.UseDynamicRendering                            = true;
    VendorAttachment.PipelineInfoMain.PipelineRenderingCreateInfo   = RecordingDeclaration;

    if (!ImGui_ImplVulkan_Init(&VendorAttachment))
    {
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the vendor attachment rejected" });
    }

    VendorAttached = true;

    // 🔴 The retained workspace seat, re-applied. `ImGui::CreateContext` above begins at the vendor's
    //    defaults and `StyleColorsDark` overwrites the lot — so a style applied once at bring-up was lost on
    //    every device rebuild, and the trapezoidal tabs reverted to stock rectangles.
    if (StyleApplied)
        Discard(ApplyWorkspaceStyle(AppliedMeasure, AppliedColour));
    Discard(ApplyInterfaceAntialiasing(Antialiasing));

    return Deliver<bool>::Result(true);
}

void InterfaceExchange::Reclaim()
{
    if (ContextSlot == nullptr)
        return;

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    if (Attached.ActiveDevice != VK_NULL_HANDLE)
        vkDeviceWaitIdle(Attached.ActiveDevice);

    // 📝 🔴 Each shutdown is gated on its own attachment having stood. The vendor shutdown asserts on a
    //    backend that was never initialised, so the failure path out of Construct used to abort the
    //    process instead of reporting the refusal it had already built.
    if (VendorAttached)
        ImGui_ImplVulkan_Shutdown();

    if (WindowAttached)
        ImGui_ImplGlfw_Shutdown();

    ImGui::DestroyContext(static_cast<ImGuiContext*>(ContextSlot));

    NativeWindow = nullptr;

    if (DescriptorSlot != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(Attached.ActiveDevice, DescriptorSlot, nullptr);
        DescriptorSlot = VK_NULL_HANDLE;
    }

    ContextSlot      = nullptr;
    TickOpen         = false;
    ContentAssembled = false;
    WorkspaceEntered = false;
    WindowAttached   = false;
    VendorAttached   = false;
    Attached         = {};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE TICK
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> InterfaceExchange::Advance()
{
    if (ContextSlot == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "no interface context is constructed" });

    if (TickOpen)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "a tick is already open" });

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    TickOpen         = true;
    ContentAssembled = false;
    WorkspaceEntered = false;

    return Deliver<bool>::Result(true);
}

Deliver<bool> InterfaceExchange::Seal()
{
    if (!TickOpen)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "no tick is open" });

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));
    LeaveWorkspaceWindow();
    ImGui::Render();

    // 📝 The look gesture's cursor warp is NOT here — it runs inside `CameraInput`, mid-frame,
    //    before `Render()` captures `MousePosPrev` (see that function's lesson). A warp after
    //    `Render()` makes the next frame's delta measure from the pre-warp position, not the centre,
    //    and the camera stops turning.
    TickOpen         = false;
    ContentAssembled = true;

    return Deliver<bool>::Result(true);
}

Deliver<bool> InterfaceExchange::Abandon()
{
    if (!TickOpen)
        return Deliver<bool>::Result(true);

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));
    LeaveWorkspaceWindow();
    ImGui::EndFrame();

    TickOpen         = false;
    ContentAssembled = false;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE WORKSPACE STYLE
//------------------------------------------------------------------------------------------------------------------------

namespace
{

/// 🧩 One Slate colour as the vendor's four unit ordinates.
/// cost  ✔️
ImVec4 Vendor(ThemeToken Colour)
{
    return ImVec4(static_cast<float>(Colour.Red)     / 255.0f,
                  static_cast<float>(Colour.Green)   / 255.0f,
                  static_cast<float>(Colour.Blue)    / 255.0f,
                  static_cast<float>(Colour.Opacity) / 255.0f);
}

}   // namespace

Deliver<bool> InterfaceExchange::ApplyWorkspaceStyle(const WorkspaceMetric& Measure, const WorkspaceColour& Tinted)
{
    // 🔴 Retained BEFORE the context is tested, so a seat asked for while no context stands is applied by
    //    the next Construct rather than silently dropped.
    AppliedMeasure = Measure;
    AppliedColour     = Tinted;
    StyleApplied   = true;

    if (ContextSlot == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no interface context stands" });

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    ImGuiStyle& Applied = ImGui::GetStyle();

    // 🔴 The four members `Patches/` adds. Each defaults to 0.0f, at which a patched build rasterises
    //    byte-identically to an unpatched one — so applying them here is what turns the sheet's trapezoid on.
    Applied.TabSlant       = Measure.TabSlant;
    Applied.TabOverlap     = Measure.TabOverlap;
    Applied.TabHeight      = Measure.TabY;
    Applied.TabStripPadTop = Measure.StripPadTop;

    // ⚠️ Coupled with TabOverlap: the sheet's 38 px padding exists to clear the slant plus the overlap.
    Applied.FramePadding.x = Measure.TabPadX;

    // 📝 The sheet rounds nothing and strokes nothing. Both configurable, both zero here, which is what
    //    `DockWorkspace.html` states — `roundCorners` is off and no tab carries a border.
    Applied.TabRounding   = Measure.TabRadius;
    Applied.TabBorderSize = Measure.TabEdgeWeight;

    Applied.Colors[ImGuiCol_Tab]               = Vendor(Tinted.TabQuiet);
    Applied.Colors[ImGuiCol_TabHovered]        = Vendor(Tinted.TabHovered);
    Applied.Colors[ImGuiCol_TabSelected]       = Vendor(Tinted.TabTaken);
    Applied.Colors[ImGuiCol_TabDimmed]         = Vendor(Tinted.TabQuiet);
    Applied.Colors[ImGuiCol_TabDimmedSelected] = Vendor(Tinted.TabTaken);
    Applied.Colors[ImGuiCol_Text]              = Vendor(Tinted.TabColourTaken);

    // 🔴 The dock node's own chrome, silenced. A docked workspace's tabs are drawn by the node, and the
    //    vendor frames them with a title bar, an overline above the selected tab and a bar border — none
    //    of which `DockWorkspace.html` has. Left applied, they read as a blue band across the strip.
    Applied.Colors[ImGuiCol_TitleBg]                   = Vendor(Tinted.StripGround);
    Applied.Colors[ImGuiCol_TitleBgActive]             = Vendor(Tinted.StripGround);
    Applied.Colors[ImGuiCol_WindowBg]                  = Vendor(Tinted.BodyGround);
    Applied.Colors[ImGuiCol_DockingEmptyBg]            = Vendor(Tinted.BodyGround);
    Applied.Colors[ImGuiCol_TabSelectedOverline]       = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    Applied.Colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    Applied.TabBarBorderSize = 0.0f;
    Applied.WindowRounding   = 0.0f;

    // 📝 The sheet's min-width and max-width. `TabMinWidthShrink` is held at the same figure so a crowded
    //    strip scrolls rather than shrcolouring its tabs below the width the slant was measured against.
    Applied.TabMinWidthBase   = Measure.TabXFloor;
    Applied.TabMinWidthShrink = Measure.TabXFloor;

    // 🔴 The close mark stands on EVERY tab, selected or not. The vendor hides it on unselected tabs until
    //    hovered; the sheet draws it on all of them, and a mark that appears only under the pointer is one
    //    the artist cannot see to aim at.
    Applied.TabCloseButtonMinWidthSelected   = -1.0f;
    Applied.TabCloseButtonMinWidthUnselected = -1.0f;

    // Docked windows retain a copy of these colours. Refresh that copy so existing tabs follow
    // every theme change, not only tabs created after the change.
    for (ImGuiWindow* Window : ImGui::GetCurrentContext()->Windows)
    {
        if (Window == nullptr)
            continue;

        Window->DockStyle.Colors[ImGuiWindowDockStyleCol_Text]                     = ImGui::ColorConvertFloat4ToU32(Applied.Colors[ImGuiCol_Text]);
        Window->DockStyle.Colors[ImGuiWindowDockStyleCol_TabHovered]               = ImGui::ColorConvertFloat4ToU32(Applied.Colors[ImGuiCol_TabHovered]);
        Window->DockStyle.Colors[ImGuiWindowDockStyleCol_TabFocused]               = ImGui::ColorConvertFloat4ToU32(Applied.Colors[ImGuiCol_Tab]);
        Window->DockStyle.Colors[ImGuiWindowDockStyleCol_TabSelected]              = ImGui::ColorConvertFloat4ToU32(Applied.Colors[ImGuiCol_TabSelected]);
        Window->DockStyle.Colors[ImGuiWindowDockStyleCol_TabSelectedOverline]      = ImGui::ColorConvertFloat4ToU32(Applied.Colors[ImGuiCol_TabSelectedOverline]);
        Window->DockStyle.Colors[ImGuiWindowDockStyleCol_TabDimmed]                = ImGui::ColorConvertFloat4ToU32(Applied.Colors[ImGuiCol_TabDimmed]);
        Window->DockStyle.Colors[ImGuiWindowDockStyleCol_TabDimmedSelected]       = ImGui::ColorConvertFloat4ToU32(Applied.Colors[ImGuiCol_TabDimmedSelected]);
        Window->DockStyle.Colors[ImGuiWindowDockStyleCol_TabDimmedSelectedOverline] = ImGui::ColorConvertFloat4ToU32(Applied.Colors[ImGuiCol_TabDimmedSelectedOverline]);
        Window->DockStyle.Colors[ImGuiWindowDockStyleCol_UnsavedMarker]            = ImGui::ColorConvertFloat4ToU32(Applied.Colors[ImGuiCol_UnsavedMarker]);
    }

    // 🔴 A disc rather than a slab, for the close mark and the strip's `+` both — `Patches/`'s PatchC.
    //    Stated as a fraction of the control's own extent, so `ScaleAllSizes` must not scale it and does
    //    not. Left unseated the member defaults to 0.0f, at which PatchC's branch never runs and both
    //    controls render as the vendor's rectangles — which is exactly how they shipped.
    Applied.TabButtonRounding = 1.0f;

    // 🔴 A workspace dragged out ALONE keeps a tab bar, so it carries the sheet's trapezoid on its grey
    //    strip instead of degrading to a plain caption. Applied on the context rather than per window
    //    because it governs the docking system as a whole.
    ImGui::GetIO().ConfigDockingAlwaysTabBar = true;

    return Deliver<bool>::Result(true);
}

bool InterfaceExchange::RecordWorkspaceAddition(const PlaneExtent&  Extent,
                                                std::uint32_t       OpenCount,
                                                std::uint32_t&      AskingNode)
{
    (void)Extent;
    (void)OpenCount;

    AskingNode = 0u;

    if (ContextSlot == nullptr || !TickOpen)
        return false;

    ImGuiContext& Current = *static_cast<ImGuiContext*>(ContextSlot);

    ImGui::SetCurrentContext(&Current);

    // 🔴 EVERY node carries a `+`, floating ones included, and each reports ITSELF when pressed. A torn-out
    //    float otherwise had no way to add a workspace beside it, and a `+` that reported only "pressed"
    //    had the caller seat the new workspace into the main dock space — so pressing it on one strip
    //    added the workspace to another window.
    bool Pressed = false;

    for (int Index = 0; Index < Current.DockContext.Nodes.Data.Size; ++Index)
    {
        ImGuiDockNode* Node = static_cast<ImGuiDockNode*>(Current.DockContext.Nodes.Data[Index].val_p);

        // ⚠️ Only a leaf that actually laid out a tab bar this tick. A split node holds no tabs, and a
        //    node whose bar was never built has nothing to amend.
        if (Node == nullptr || Node->IsSplitNode() || Node->TabBar == nullptr)
            continue;

        if (!ImGui::DockNodeBeginAmendTabBar(Node))
            continue;

        // 🔴 NO section flag. `Trailing` right-aligns a button against the bar's far edge — yards from the
        //    last tab, with the scroll arrows between — and the sheet applies its `addBtn` immediately after
        //    the tabs. An unflagged button lands in the central section, which flows straight on from the
        //    last tab, and it scrolls with them.
        // 📝 The identity is scoped by `DockNodeBeginAmendTabBar`'s own `PushOverrideID(node->ID)`, so two
        //    strips cannot collide; an extra PushID here would only add a second seed to no purpose.
        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_NoTooltip))
        {
            Pressed    = true;
            AskingNode = static_cast<std::uint32_t>(Node->ID);
        }

        ImGui::DockNodeEndAmendTabBar();
    }

    return Pressed;
}

bool InterfaceExchange::VacantPressed(const PlaneExtent& Extent)
{
    if (ContextSlot == nullptr || !TickOpen)
        return false;

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    ImGui::SetNextWindowPos(ImVec2(Extent.MinimumX, Extent.MinimumY));
    ImGui::SetNextWindowSize(ImVec2(Extent.Width(), Extent.Height()));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.03f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.0f, 1.0f, 1.0f, 0.06f));

    bool Pressed = false;

    if (ImGui::Begin("SlateVacantShell", nullptr, ImGuiWindowFlags_NoTitleBar
                                                | ImGuiWindowFlags_NoResize
                                                | ImGuiWindowFlags_NoMove
                                                | ImGuiWindowFlags_NoScrollbar
                                                | ImGuiWindowFlags_NoSavedSettings
                                                | ImGuiWindowFlags_NoDocking
                                                | ImGuiWindowFlags_NoBringToFrontOnFocus
                                                | ImGuiWindowFlags_NoBackground))
    {
        // 📝 An invisible full-extent control. The run itself is recorded by `WorkspacePanel` on the shell
        //    ground, so this contributes the hit area and nothing visible but the hover wash.
        Pressed = ImGui::Button("##SlateCreatePanel", ImVec2(-1.0f, -1.0f));
    }

    ImGui::End();

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    return Pressed;
}

Deliver<bool> InterfaceExchange::ApplyInterfaceAntialiasing(InterfaceAntialiasing Preference)
{
    Antialiasing = Preference;

    if (ContextSlot == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no interface context stands" });

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));
    ImGuiStyle& Applied = ImGui::GetStyle();

    switch (Preference)
    {
        case InterfaceAntialiasing::Refined:
            Applied.AntiAliasedLines       = true;
            Applied.AntiAliasedLinesUseTex = true;
            Applied.AntiAliasedFill        = true;
            Applied.CurveTessellationMaxError  = 0.50f;
            Applied.CircleTessellationMaxError = 0.20f;
            break;
        case InterfaceAntialiasing::Basic:
            Applied.AntiAliasedLines       = true;
            Applied.AntiAliasedLinesUseTex = false;
            Applied.AntiAliasedFill        = true;
            Applied.CurveTessellationMaxError  = 1.25f;
            Applied.CircleTessellationMaxError = 0.50f;
            break;
        case InterfaceAntialiasing::None:
            Applied.AntiAliasedLines       = false;
            Applied.AntiAliasedLinesUseTex = false;
            Applied.AntiAliasedFill        = false;
            Applied.CurveTessellationMaxError  = 2.00f;
            Applied.CircleTessellationMaxError = 1.00f;
            break;
        default:
            Antialiasing = InterfaceAntialiasing::Refined;
            return ApplyInterfaceAntialiasing(Antialiasing);
    }

    return Deliver<bool>::Result(true);
}

void InterfaceExchange::RecordDockSpace(const PlaneExtent& Extent)
{
    if (ContextSlot == nullptr || !TickOpen)
        return;

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    ImGui::SetNextWindowPos(ImVec2(Extent.MinimumX, Extent.MinimumY));
    ImGui::SetNextWindowSize(ImVec2(Extent.Width(), Extent.Height()));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    const ImGuiWindowFlags Bare = ImGuiWindowFlags_NoTitleBar
                                | ImGuiWindowFlags_NoResize
                                | ImGuiWindowFlags_NoMove
                                | ImGuiWindowFlags_NoScrollbar
                                | ImGuiWindowFlags_NoScrollWithMouse
                                | ImGuiWindowFlags_NoSavedSettings
                                | ImGuiWindowFlags_NoBringToFrontOnFocus
                                | ImGuiWindowFlags_NoNavFocus
                                | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("SlateWorkspaceBody", nullptr, Bare))
    {
        // 🔴 `PassthruCentralNode` so the workspace ground shows through where nothing is docked. Without
        //    it the vendor fills the whole node with its own colour and the sheet's OLED body is lost.
        // 🔴 `PassthruCentralNode` so the workspace ground shows through where nothing is docked; the two
        //    button flags remove the node's own close widget and menu triangle, which the sheet has not.
        // 📝 The two button flags live in `ImGuiDockNodeFlagsPrivate_`, so the set is composed as the
        //    integral type the parameter takes rather than by mixing two enumerations, which C++20
        //    deprecates and `-Wdeprecated-enum-enum-conversion` reports.
        const ImGuiDockNodeFlags NodeFlags =
              static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_PassthruCentralNode)
            | static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoCloseButton)
            | static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoWindowMenuButton);

        ImGui::DockSpace(ImGui::GetID("SlateDockSpace"), ImVec2(0.0f, 0.0f), NodeFlags);
    }

    ImGui::End();

    ImGui::PopStyleVar(2);
}

void InterfaceExchange::RecordWorkspaceWindow(const char* Titled,
                                                bool Docked,
                                                std::uint32_t IntoNode,
                                                bool& Opened)
{
    static_cast<void>(EnterWorkspaceWindow(Titled, Docked, IntoNode, Opened));
    LeaveWorkspaceWindow();
}

PlaneExtent InterfaceExchange::EnterWorkspaceWindow(const char* Titled,
                                                     bool Docked,
                                                     std::uint32_t IntoNode,
                                                     bool& Opened)
{
    if (ContextSlot == nullptr || !TickOpen || Titled == nullptr || WorkspaceEntered)
        return {};

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    // 🔴 Applied into the node that ASKED for it, not always the main dock space. A workspace registered by
    //    a torn-out float's `+` otherwise appeared in the other window, which is not where the artist
    //    pressed. Zero means no node was named and the main space is correct.
    if (Docked)
    {
        const ImGuiID Destination = (IntoNode != 0u)
                                  ? static_cast<ImGuiID>(IntoNode)
                                  : ImGui::GetID("SlateDockSpace");

        ImGui::SetNextWindowDockID(Destination, ImGuiCond_Always);
    }

    // 🔴 ⚠️ `NoTitleBar` is deliberately NOT set. `GetWindowAlwaysWantOwnTabBar` refuses a tab bar to any
    //    window carrying it, so hiding the caption that way also denied a lone floating workspace the
    //    strip that gives it the sheet's trapezoid. Docked, the node's tab bar replaces the caption;
    //    floating, the window gets a tab bar of its own and no caption is drawn either.
    ImGuiWindowClass Declared;
    Declared.DockingAlwaysTabBar = true;

    // 📝 A lone float's own node gets the same two silences as the main dock space, so a torn-off
    //    workspace does not sprout the caret and close widget the docked one has none of.
    Declared.DockNodeFlagsOverrideSet = static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoWindowMenuButton)
                                      | static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoCloseButton);

    ImGui::SetNextWindowClass(&Declared);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    const bool Visible = ImGui::Begin(Titled, &Opened, ImGuiWindowFlags_NoScrollbar
                                                           | ImGuiWindowFlags_NoScrollWithMouse
                                                           | ImGuiWindowFlags_NoCollapse);
    WorkspaceEntered = true;

    if (!Visible)
        return {};

    const ImVec2 Origin    = ImGui::GetCursorScreenPos();
    const ImVec2 Available = ImGui::GetContentRegionAvail();
    return { Origin.x, Origin.y, Origin.x + Available.x, Origin.y + Available.y };
}

void InterfaceExchange::LeaveWorkspaceWindow()
{
    if (ContextSlot == nullptr || !WorkspaceEntered)
        return;

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));
    ImGui::End();
    ImGui::PopStyleVar();
    WorkspaceEntered = false;
}

bool InterfaceExchange::WorkspaceCurrent(const char* Titled) const
{
    if (ContextSlot == nullptr || !TickOpen || Titled == nullptr)
        return false;

    ImGuiContext* Context = static_cast<ImGuiContext*>(ContextSlot);
    ImGuiWindow*  Window  = ImGui::FindWindowByName(Titled);
    if (Window == nullptr)
        return false;

    if (Window->DockNode != nullptr)
        return Window->DockNode->SelectedTabId == Window->TabId;

    return (Context->NavWindow == Window);
}

Deliver<bool> InterfaceExchange::Renegotiate(std::uint32_t MinimumImageCount, std::uint32_t ImageCount)
{
    if (ContextSlot == nullptr || !VendorAttached)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "no vendor attachment stands" });

    if (MinimumImageCount < 2u || ImageCount < MinimumImageCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the display image counts are inconsistent" });

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    // Dear ImGui 1.92.9 exposes runtime renegotiation of the requested minimum. The actual count is supplied
    //    at construction and retained here beside the new chain count; it is never replaced by Slate's slot count.
    ImGui_ImplVulkan_SetMinImageCount(MinimumImageCount);

    Attached.MinimumDisplayImageCount = MinimumImageCount;
    Attached.DisplayImageCount        = ImageCount;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      RECORDING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> InterfaceExchange::Record(VkCommandBuffer CommandRecording)
{
    if (!ContentAssembled)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "nothing has been sealed for recording" });

    if (CommandRecording == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "no command recording was supplied" });

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    ImDrawData* AssembledContent = ImGui::GetDrawData();

    if (AssembledContent == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the sealed content was not assembled" });

    // 📝 A display extent of zero — a minimised window — assembles content that covers nothing. Recording it
    //    is legal and costs a recorded nothing; skipping it here keeps the recording out of the rotation's
    //    measured duration entirely.
    if (AssembledContent->DisplaySize.x <= 0.0f || AssembledContent->DisplaySize.y <= 0.0f)
    {
        ContentAssembled = false;
        return Deliver<bool>::Result(true);
    }

    ImGui_ImplVulkan_RenderDrawDataBand(AssembledContent, CommandRecording, 0);
    ContentAssembled = false;

    return Deliver<bool>::Result(true);
}

Deliver<bool> InterfaceExchange::RecordBeneath(VkCommandBuffer CommandRecording)
{
    if (!ContentAssembled)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "nothing has been sealed for recording" });

    if (CommandRecording == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "no command recording was supplied" });

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    ImDrawData* AssembledContent = ImGui::GetDrawData();

    if (AssembledContent == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the sealed content was not assembled" });

    if (AssembledContent->DisplaySize.x <= 0.0f || AssembledContent->DisplaySize.y <= 0.0f)
        return Deliver<bool>::Result(true);

    ImGui_ImplVulkan_RenderDrawDataBand(AssembledContent, CommandRecording, 1);
    return Deliver<bool>::Result(true);
}

Deliver<bool> InterfaceExchange::RecordAbove(VkCommandBuffer CommandRecording)
{
    if (!ContentAssembled)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "nothing has been sealed for recording" });

    if (CommandRecording == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "no command recording was supplied" });

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    ImDrawData* AssembledContent = ImGui::GetDrawData();

    if (AssembledContent == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the sealed content was not assembled" });

    if (AssembledContent->DisplaySize.x <= 0.0f || AssembledContent->DisplaySize.y <= 0.0f)
    {
        ContentAssembled = false;
        return Deliver<bool>::Result(true);
    }

    ImGui_ImplVulkan_RenderDrawDataBand(AssembledContent, CommandRecording, 2);
    ContentAssembled = false;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CAPTURE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The capture reads go through SetCurrentContext rather than reaching into the context directly. Only
//    `imgui_internal.h` defines ImGuiContext; `imgui.h` forward-declares it, so a member read here would not
//    compile without dragging the internal header into the seam. The accessor route reads the same two bits.
bool InterfaceExchange::PointerCaptured() const
{
    if (ContextSlot == nullptr)
        return false;

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    return ImGui::GetIO().WantCaptureMouse;
}

void InterfaceExchange::WithholdPointer()
{
    if (ContextSlot == nullptr)
        return;

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    // 📝 Both flags. `WantCaptureMouse` is what a host reads to decide whether the interface owns the
    //    contact; `WantCaptureMouseUnlessPopupClose` is the variant the vendor's own widgets consult, and
    //    leaving it raised let a window under the drawer still act on the click.
    ImGuiIO& Sampled = ImGui::GetIO();

    Sampled.WantCaptureMouse                  = false;
    Sampled.WantCaptureMouseUnlessPopupClose  = false;

    // 🔴 The flags alone are not enough. They say who SHOULD receive the next contact; they do nothing
    //    about a widget that has already grabbed this one. A tab dragged out, or a window being moved, is
    //    held by the active identity and by `MovingWindow` — so a drag begun on a drawer's tongue while a
    //    tab sat beneath it moved BOTH, the drawer by its own arbitration and the tab by the vendor's.
    // ⚠️ Released rather than suppressed. The vendor re-acquires whatever the pointer is genuinely over
    //    on the next tick, so nothing here has to be restored.
    // 🔴 The active identity is released whatever holds it, not only a window move. A RESIZE grip sets
    //    `ActiveId` and leaves `MovingWindow` null, so a drawer dragged from a display corner also
    //    stretched the window beneath it — the drawer travelled by Slate's arbitration and the window
    //    resized by the vendor's, from one contact.
    ImGuiContext& Current = *static_cast<ImGuiContext*>(ContextSlot);

    if (Current.ActiveId != 0)
        ImGui::ClearActiveID();

    Current.MovingWindow = nullptr;

    // 📝 The hovered identity too. Left standing, the resize grip the pointer crossed keeps its cursor and
    //    re-seizes the contact the moment the drawer lets go of it.
    Current.HoveredId             = 0;
    Current.HoveredIdAllowOverlap = false;

    // 📝 The tab bar's own reorder request, which lives beside the active identity rather than in it. A
    //    tab already being shuffled along its strip keeps travelling on this alone.
    if (Current.CurrentTabBar != nullptr)
    {
        Current.CurrentTabBar->ReorderRequestTabId = 0;
        Current.CurrentTabBar->ReorderRequestOffset = 0;
    }
}

bool InterfaceExchange::KeyboardCaptured() const
{
    if (ContextSlot == nullptr)
        return false;

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    return ImGui::GetIO().WantCaptureKeyboard;
}

bool InterfaceExchange::KeyPressed(KeySubject Subject) const
{
    if (ContextSlot == nullptr || !TickOpen)
        return false;

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    // 🔴 The reference's own guard, stated once here rather than at every call site: a Tab typed into a
    //    filter field belongs to the field. `WantTextInput` and not `WantCaptureKeyboard` — the latter is
    //    raised by any hovered window, which would swallow the summon over the whole shell.
    if (ImGui::GetIO().WantTextInput)
        return false;

    ImGuiKey Arbitrated = ImGuiKey_None;

    switch (Subject)
    {
        case KeySubject::Summon:   Arbitrated = ImGuiKey_Tab;       break;
        case KeySubject::Withdraw: Arbitrated = ImGuiKey_Escape;    break;
        case KeySubject::Retract:  Arbitrated = ImGuiKey_Backspace; break;

        case KeySubject::DeclareTexturing:      Arbitrated = ImGuiKey_P;          break;
        case KeySubject::DeclareFill:       Arbitrated = ImGuiKey_F;          break;
        case KeySubject::DeclareAdjustment: Arbitrated = ImGuiKey_A;          break;
        case KeySubject::DeclareRetention:  Arbitrated = ImGuiKey_R;          break;
        case KeySubject::DeclareDecal:      Arbitrated = ImGuiKey_D;          break;
        case KeySubject::DeclarePattern:    Arbitrated = ImGuiKey_T;          break;
        case KeySubject::DeclareFolder:     Arbitrated = ImGuiKey_G;          break;
        case KeySubject::AttachMask:        Arbitrated = ImGuiKey_M;          break;
        case KeySubject::Secure:            Arbitrated = ImGuiKey_L;          break;
        case KeySubject::Solo:              Arbitrated = ImGuiKey_S;          break;
        case KeySubject::Conceal:           Arbitrated = ImGuiKey_H;          break;
        case KeySubject::Seek:              Arbitrated = ImGuiKey_Slash;      break;
        case KeySubject::Rename:            Arbitrated = ImGuiKey_F2;         break;
        case KeySubject::Unfold:            Arbitrated = ImGuiKey_Space;      break;
        case KeySubject::Retire:            Arbitrated = ImGuiKey_Delete;     break;
        case KeySubject::StepPrior:         Arbitrated = ImGuiKey_UpArrow;    break;
        case KeySubject::StepNext:          Arbitrated = ImGuiKey_DownArrow;  break;
        case KeySubject::Disclose:          Arbitrated = ImGuiKey_RightArrow; break;
        case KeySubject::Withhold:          Arbitrated = ImGuiKey_LeftArrow;  break;
        case KeySubject::Revert:            Arbitrated = ImGuiKey_Z;          break;
        case KeySubject::ChooseSelect:      Arbitrated = ImGuiKey_Q;          break;

        default:                   return false;
    }

    // 📝 Backspace alone repeats. Holding it to clear a filter is what an artist expects; holding Tab to
    //    flap the inspector is not, so the two are asked for on different terms.
    // 📐 The four arrows repeat on the same grounds: holding one walks the arrangement, which is what the
    //    reference's own `ArrowUp`/`ArrowDown` branch does under the window system's own repeat.
    if (Subject == KeySubject::Retract   || Subject == KeySubject::StepPrior ||
        Subject == KeySubject::StepNext  || Subject == KeySubject::Disclose  ||
        Subject == KeySubject::Withhold)
    {
        return ImGui::IsKeyPressed(Arbitrated, true);
    }

    // 📝 Unrepeated. The vendor's default repeat would carry the inspector back and forth for as long as
    //    the artist rested a finger on Tab.
    return ImGui::IsKeyPressed(Arbitrated, false);
}

ModifierCondition InterfaceExchange::Modifiers() const
{
    ModifierCondition Current;

    if (ContextSlot == nullptr || !TickOpen)
        return Current;

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    const ImGuiIO& Sampled = ImGui::GetIO();

    // 📐 `e.metaKey||e.ctrlKey`, exactly as the reference folds the two. The vendor already resolves
    //    Command on macOS and Control elsewhere into `KeyCtrl`, so the fold is one reading here.
    Current.Commanded = Sampled.KeyCtrl || Sampled.KeySuper;
    Current.Shifted   = Sampled.KeyShift;
    Current.Alternate = Sampled.KeyAlt;

    return Current;
}

CameraCondition InterfaceExchange::CameraInput(bool LookPermitted)
{
    CameraCondition Current;

    if (ContextSlot == nullptr || !TickOpen)
        return Current;

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    const ImGuiIO& Sampled = ImGui::GetIO();

    // 🔴 The movement keys are gated on a run the artist is typing into — W inside a text field is a
    //    letter, not a fly. The LOOK gesture is deliberately NOT gated: in this codebase a hovered
    //    window raises `WantCaptureKeyboard`, and gating the look on it would stop the camera the
    //    moment the pointer crossed a panel — the "turns then stops" defect. The fly camera owns the
    //    right button; the panels own the left one.
    const bool Typing = Sampled.WantTextInput;

    // 📐 The look gesture is the right button held: while it stands, the pointer's travel is the
    //    camera's turn, exactly as the reference fly-cams read it.
    Current.LookHeld = LookPermitted && ImGui::IsMouseDown(ImGuiMouseButton_Right);

    // 🔴 THE FLY KEYS BELONG TO THE FLY GESTURE. They were read whenever the artist was not typing,
    //    so W A S D E Q moved the camera at all times -- and Q, which is the Select tool, ALSO sank the
    //    camera every time it was pressed. Every reference fly-cam this was modelled on (Unreal, Unity)
    //    reads WASD only while the right button is held, for exactly this reason: an editor needs its
    //    letters for tools.
    //
    // ⚠️ `Typing` still gates them, because a right-drag with a text field focused must not type
    //    into it AND fly. The two guards answer different questions and both are needed.
    if (!Typing && Current.LookHeld)
    {
        Current.ForwardHeld  = ImGui::IsKeyDown(ImGuiKey_W);
        Current.BackwardHeld = ImGui::IsKeyDown(ImGuiKey_S);
        Current.LeftHeld     = ImGui::IsKeyDown(ImGuiKey_A);
        Current.RightHeld    = ImGui::IsKeyDown(ImGuiKey_D);
        Current.UpHeld       = ImGui::IsKeyDown(ImGuiKey_E);
        Current.DownHeld     = ImGui::IsKeyDown(ImGuiKey_Q);
        Current.ShiftHeld    = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
    }
    Current.SpeedSteps = Current.LookHeld ? Sampled.MouseWheel : 0.0f;

    // 🔴 THE LOOK TRACKS THE OS CURSOR ITSELF and never reads `io.MouseDelta`. ImGui's delta is
    //    computed at `NewFrame` from the position the backend's cursor callback reported at the last
    //    `glfwPollEvents`; synthetic cursor updates can make that delta alternate or measure from
    //    the wrong location. The seam therefore reads `glfwGetCursorPos` directly and measures
    //    against its own previous real sample without synthesising cursor movement.
    if (NativeWindow != nullptr)
    {
        GLFWwindow* const Window = static_cast<GLFWwindow*>(NativeWindow);
        double CursorX = 0.0;
        double CursorY = 0.0;
        glfwGetCursorPos(Window, &CursorX, &CursorY);

        if (Current.LookHeld)
        {
            // 🔴 The arrival frame establishes capture; it is not motion. Measuring it against the
            //    previous free cursor sample turns a plain right-click into one synthetic look step.
            if (LookWasHeld)
            {
                Current.LookDeltaX = static_cast<float>(CursorX - LookLastX);
                Current.LookDeltaY = static_cast<float>(CursorY - LookLastY);
            }

            // Do not warp or hide the OS cursor during look. Warping it to the window centre can
            // feed the resize-divider hit test an artificial edge/centre sample, and it also makes
            // the pointer appear to disappear while the artist is merely moving the mouse. The
            // direct OS samples above already provide a stable relative delta, so retain the real
            // position and let the next tick measure from it.
            LookLastX = CursorX;
            LookLastY = CursorY;
            LookWasHeld = true;

            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        }
        else
        {
            // 📝 The capture is released with the gesture: no warp, and the tracking follows the
            //    cursor so the next press starts from where the pointer actually is.
            LookLastX = CursorX;
            LookLastY = CursorY;
            LookWasHeld = false;
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        }
    }

    return Current;
}

bool InterfaceExchange::AcceptTyped(char* Intake, std::uint32_t Limit) const
{
    if (ContextSlot == nullptr || !TickOpen || Intake == nullptr || Limit == 0u)
        return false;

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ContextSlot));

    const ImGuiIO& Sampled = ImGui::GetIO();

    // 📝 The run's standing extent, found rather than carried, so a caller may seed the field with a
    //    literal and never has to keep a length beside it.
    std::uint32_t Occupied = 0u;

    while (Occupied + 1u < Limit && Intake[Occupied] != '\0')
        ++Occupied;

    bool Accepted = false;

    for (int Index = 0; Index < Sampled.InputQueueCharacters.Size; ++Index)
    {
        const ImWchar Typed = Sampled.InputQueueCharacters[Index];

        // 🔴 Printable ASCII only, and the terminator's byte is reserved before the test — a run written
        //    to its very last byte with no room for the terminator is the classic off-by-one this avoids.
        if (Typed < 0x20 || Typed > 0x7E)
            continue;

        if (Occupied + 1u >= Limit)
            break;

        Intake[Occupied++] = static_cast<char>(Typed);
        Accepted           = true;
    }

    Intake[Occupied] = '\0';

    return Accepted;
}

// 📝 Defined here, in the exchange's own translation unit, and not in RecordingSurface.cpp: this is
//    the unit that names the vendor's Vulkan attachment, and the harness proofs that compile the
//    recording surface without the exchange must not pull the attachment's symbols in.
std::uintptr_t RecordingSurface::RegisterSampledImage(VkSampler Sampler, VkImageView ImageView)
{
    // 🔴 The registration rides the CURRENT context, which is the exchange's own at the two call
    //    sites (startup, after the interface context is constructed, and the device-recovery branch,
    //    where the rebuilt exchange has just set it). Guarding on it here means a host that calls
    //    outside that window gets a zero identity instead of a registration into a stranger's pool.
    if (Sampler == VK_NULL_HANDLE || ImageView == VK_NULL_HANDLE || ImGui::GetCurrentContext() == nullptr)
        return 0u;

    // 📝 The identity is the vendor's descriptor set, carried opaque: the callers draw with it and
    //    never see its type, and the descriptor pool that owns it is the interface's own — it dies
    //    with this exchange, which is why a device rebuild re-registers the texture.
    const VkDescriptorSet Set = ImGui_ImplVulkan_AddTexture(Sampler, ImageView,
                                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return static_cast<std::uintptr_t>(reinterpret_cast<std::uint64_t>(Set));
}

}   // namespace Slate
