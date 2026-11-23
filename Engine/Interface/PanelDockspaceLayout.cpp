/*==========================================================================================================================================
                                                       PANELDOCKSPACELAYOUT.CPP
==========================================================================================================================================*/
// 🧩 Archive validated native dock regions in TOML and reconstruct them through ImGui's existing DockBuilder.

#include "PanelDockspace.h"
#include <algorithm>
#include <cmath>
#include <imgui_internal.h>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace Frontier
{
namespace
{
void Require(bool Condition, const char *Message)
{
    if (!Condition)
        throw std::runtime_error(Message);
}
std::string Key(const toml::node_view<const toml::node> &Field)
{
    auto Text = Field.value<std::string>();
    Require(Text && !Text->empty() && Text->size() <= 64, "Invalid layout panel key");
    for (const auto Character : *Text)
        Require((Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') ||
                    (Character >= '0' && Character <= '9') || Character == '-' || Character == '_',
                "Invalid layout panel key");
    return *Text;
}
double Number(const toml::table &Section, const char *Name, double Minimum, double Maximum)
{
    const auto Amount = Section[Name].value<double>();
    Require(Amount && std::isfinite(*Amount) && *Amount >= Minimum && *Amount <= Maximum, "Invalid layout geometry");
    return *Amount;
}
void Rectangle(const toml::table &Section)
{
    (void)Number(Section, "x", -1000000, 1000000);
    (void)Number(Section, "y", -1000000, 1000000);
    (void)Number(Section, "width", 1, 32768);
    (void)Number(Section, "height", 1, 32768);
    Require(Section["native"].is_boolean(), "Invalid native window marker");
    if (Section.contains("monitor"))
    {
        const auto *Monitor = Section["monitor"].as_table();
        Require(Monitor != nullptr, "Invalid monitor geometry");
        (void)Number(*Monitor, "x", -1000000, 1000000);
        (void)Number(*Monitor, "y", -1000000, 1000000);
        (void)Number(*Monitor, "width", 1, 32768);
        (void)Number(*Monitor, "height", 1, 32768);
        (void)Number(*Monitor, "dpi", .25, 8);
        (void)Number(*Monitor, "window_x", -1000000, 1000000);
        (void)Number(*Monitor, "window_y", -1000000, 1000000);
    }
}
double Quantize(float Amount)
{
    return std::round(static_cast<double>(Amount) * 1000) / 1000;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    ARCHIVE VALIDATION
//------------------------------------------------------------------------------------------------------------------------

bool PanelDockspace::ValidateLayout(const toml::table &Layout, std::string &Diagnostic) noexcept
{
    try
    {
        std::ostringstream Encoded;
        Encoded << Layout;
        Require(Encoded.str().size() <= 65536, "Workspace layout exceeds 64 KiB limit");
        Require(!Layout.contains("browser_drawer_open") || Layout["browser_drawer_open"].is_boolean(),
                "Invalid browser drawer visibility");
        Require(Layout["format"].value<int64_t>() == 1, "Unsupported workspace layout format");
        const auto *Panels  = Layout["panels"].as_array();
        const auto *Regions = Layout["regions"].as_array();
        const auto *Roots   = Layout["roots"].as_array();
        Require(Panels && Panels->size() <= 16 && Regions && !Regions->empty() && Regions->size() <= 63 && Roots &&
                    !Roots->empty() && Roots->size() <= 17,
                "Invalid layout archive bounds");
        std::map<std::string, bool> OpenPanels;
        for (const auto &Panel : *Panels)
        {
            const auto *Section = Panel.as_table();
            Require(Section && (*Section)["open"].is_boolean(), "Invalid layout panel");
            Require(OpenPanels.emplace(Key((*Section)["key"]), (*Section)["open"].value_or(false)).second,
                    "Duplicate layout panel");
            Rectangle(*Section);
            Require(!Section->contains("docked") || (*Section)["docked"].is_boolean(), "Invalid panel docking marker");
        }
        std::set<std::string> Docked;
        std::set<size_t>      Visited;
        const auto            Traverse = [&](auto &&Self, size_t Index, size_t Depth) -> void
        {
            Require(Index < Regions->size() && Depth <= 16 && Visited.insert(Index).second,
                    "Cyclic, shared or out-of-range layout region");
            const auto *Section = (*Regions)[Index].as_table();
            Require(Section, "Invalid layout region");
            const auto Axis = (*Section)["axis"].value<std::string>();
            Require(Axis && (*Axis == "none" || *Axis == "x" || *Axis == "y"), "Invalid layout split axis");
            if (*Axis == "none")
            {
                const auto *Tabs = (*Section)["tabs"].as_array();
                Require(Tabs && Tabs->size() <= 16, "Invalid layout tab sequence");
                std::set<std::string> Local;
                for (const auto &Tab : *Tabs)
                {
                    auto Name = Key(toml::node_view<const toml::node>(&Tab));
                    Require(OpenPanels.contains(Name) && Docked.insert(Name).second,
                            "Unknown or duplicated layout tab");
                    Local.insert(Name);
                }
                const auto Selected = (*Section)["selected"].value<std::string>();
                Require(Selected && (Selected->empty() || Local.contains(*Selected)), "Invalid selected layout tab");
            }
            else
            {
                (void)Number(*Section, "ratio", .01, .99);
                const auto First  = (*Section)["first"].value<int64_t>();
                const auto Second = (*Section)["second"].value<int64_t>();
                Require(First && Second && *First >= 0 && *Second >= 0, "Invalid layout split links");
                Self(Self, static_cast<size_t>(*First), Depth + 1);
                Self(Self, static_cast<size_t>(*Second), Depth + 1);
            }
        };
        size_t MainCount = 0;
        for (const auto &Root : *Roots)
        {
            const auto *Section = Root.as_table();
            Require(Section && (*Section)["main"].is_boolean(), "Invalid layout root");
            Rectangle(*Section);
            const auto Index = (*Section)["region"].value<int64_t>();
            Require(Index && *Index >= 0, "Invalid layout root link");
            MainCount += (*Section)["main"].value_or(false) ? 1 : 0;
            Traverse(Traverse, static_cast<size_t>(*Index), 0);
        }
        Require(MainCount == 1 && Visited.size() == Regions->size(), "Unreachable layout regions or missing main root");
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}

bool PanelDockspace::ConfigureLayout(const toml::table &Layout, std::string &Diagnostic)
{
    if (Seated || !ValidateLayout(Layout, Diagnostic))
        return false;
    for (const auto &Panel : *Layout["panels"].as_array())
    {
        const auto Name = Panel.as_table()->at("key").value<std::string>();
        if (std::none_of(Entries.begin(), Entries.end(),
                         [&](const auto &Entry) { return Entry.Specification.Key == *Name; }))
        {
            Diagnostic = "Layout references an unavailable panel";
            return false;
        }
    }
    PendingLayout = Layout;
    return true;
}

void PanelDockspace::ResetLayout() noexcept
{
    PendingLayout.clear();
    SelectedPanels.clear();
    SelectionFrames = 0;
    Seated          = false;
    for (auto &Entry : Entries)
    {
        Entry.Placement    = -1;
        Entry.NativeWindow = Entry.RestoreGeometry = false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    NATIVE DOCK SNAPSHOT
//------------------------------------------------------------------------------------------------------------------------

toml::table PanelDockspace::EncodeLayout() const
{
    if (!Seated)
        return {};
    toml::array Regions, Roots, Panels;
    const auto *Main   = ImGui::GetMainViewport();
    const auto  Bounds = [&](ImVec2 Position, ImVec2 Size, bool Native)
    {
        auto Rectangle = toml::table{{"x", Quantize((Position.x - Main->Pos.x) / LayoutScale)},
                                     {"y", Quantize((Position.y - Main->Pos.y) / LayoutScale)},
                                     {"width", Quantize(std::max(1.0f, Size.x) / LayoutScale)},
                                     {"height", Quantize(std::max(1.0f, Size.y) / LayoutScale)},
                                     {"native", Native}};
        if (Native)
        {
            auto Monitor = CaptureMonitor({Position.x, Position.y, Size.x, Size.y});
            if (!Monitor.empty())
                Rectangle.insert("monitor", std::move(Monitor));
        }
        return Rectangle;
    };
    const auto PanelFor = [&](ImGuiWindow *Window) -> const DockPanelEntry *
    {
        if (Window)
            for (const auto &Entry : Entries)
                if (Window->ID == ImHashStr(Entry.WindowName.c_str()))
                    return &Entry;
        return nullptr;
    };
    // 📝 The live ImGui dock node graph is walked by raw pointer below; it is supposed to be a simple
    // binary split tree, but a corrupted/merged docking state (observed in the field as the
    // "Cyclic, shared or out-of-range layout region" rejection a few lines later) can leave a node
    // reachable from more than one parent, or even from itself. Without a visited-set guard, that
    // walk recurses without bound, pushing a fresh Regions entry on every call until the process runs
    // out of memory (a genuine std::bad_alloc) well before ValidateLayout ever gets a chance to reject
    // the result. Cap recursion depth and refuse to re-enter an already-visited node — on a healthy
    // tree neither limit is ever reached; on a corrupted one we fall back to an empty leaf for the
    // offending branch so EncodeLayout always returns a small, finite table for ValidateLayout to
    // examine (and reject) instead of ever growing unbounded.
    std::set<ImGuiDockNode *> VisitedNodes;
    const auto Capture = [&](auto &&Self, ImGuiDockNode *Region, int Depth = 0) -> size_t
    {
        const auto Index = Regions.size();
        Regions.push_back(toml::table{});
        toml::table Section;
        const bool Corrupted = Depth > 32 || (Region && !VisitedNodes.insert(Region).second);
        if (Region && !Corrupted && Region->IsSplitNode())
        {
            const bool Horizontal = Region->SplitAxis == ImGuiAxis_X;
            const auto First      = Region->ChildNodes[0];
            const auto Second     = Region->ChildNodes[1];
            const auto Across =
                Horizontal ? First->SizeRef.x + Second->SizeRef.x : First->SizeRef.y + Second->SizeRef.y;
            const auto Extent = Horizontal ? First->SizeRef.x : First->SizeRef.y;
            Section           = toml::table{{"axis", Horizontal ? "x" : "y"},
                                            {"ratio", std::clamp(Quantize(Extent / std::max(1.0f, Across)), .01, .99)},
                                            {"first", static_cast<int64_t>(Self(Self, First, Depth + 1))},
                                            {"second", static_cast<int64_t>(Self(Self, Second, Depth + 1))}};
        }
        else if (Corrupted)
        {
            // 📝 Dead branch of a corrupted tree: emit a harmless empty leaf rather than recursing
            // further or dereferencing a node we cannot trust. ValidateLayout will still reject the
            // overall table (the real fix for the underlying dock corruption is tracked separately),
            // but this guarantees EncodeLayout itself always terminates and stays bounded in size.
            Section = toml::table{{"axis", "none"}, {"tabs", toml::array{}}, {"selected", std::string()}};
        }
        else
        {
            toml::array Tabs;
            std::string Selected;
            const auto  Append = [&](ImGuiWindow *Window)
            {
                if (const auto *Entry = PanelFor(Window))
                {
                    Tabs.push_back(Entry->Specification.Key);
                    if (Region->SelectedTabId == Window->TabId)
                        Selected = Entry->Specification.Key;
                }
            };
            if (Region && Region->TabBar)
            {
                for (const auto &Tab : Region->TabBar->Tabs)
                    Append(Tab.Window);
            }
            else if (Region)
                for (auto *Window : Region->Windows)
                    Append(Window);
            Section = toml::table{{"axis", "none"}, {"tabs", std::move(Tabs)}, {"selected", Selected}};
        }
        *Regions[Index].as_table() = std::move(Section);
        return Index;
    };
    auto *MainRegion  = ImGui::DockBuilderGetNode(Dockspace);
    auto  MainSection = Bounds(Main->WorkPos, Main->WorkSize, false);
    MainSection.insert("main", true);
    MainSection.insert("region", static_cast<int64_t>(Capture(Capture, MainRegion)));
    Roots.push_back(std::move(MainSection));
    std::set<ImGuiID> Captured{Dockspace};
    for (const auto &Entry : Entries)
    {
        auto      *Window = ImGui::FindWindowByName(Entry.WindowName.c_str());
        const bool Native = Window && Window->Viewport && Window->Viewport != Main;
        auto       Panel  = Bounds(Window ? Window->Pos : ImVec2(Main->Pos.x + 64, Main->Pos.y + 64),
                                   Window ? Window->SizeFull : ImVec2(640 * LayoutScale, 520 * LayoutScale), Native);
        Panel.insert("key", Entry.Specification.Key);
        Panel.insert("open", Entry.Specification.Open);
        Panel.insert("docked", Window && Window->DockId != 0);
        Panels.push_back(std::move(Panel));
        if (Window && Window->DockNode)
        {
            auto *Root = ImGui::DockNodeGetRootNode(Window->DockNode);
            if (Captured.insert(Root->ID).second)
            {
                auto Section = Bounds(Root->Pos, Root->Size, Native);
                Section.insert("main", false);
                Section.insert("region", static_cast<int64_t>(Capture(Capture, Root)));
                Roots.push_back(std::move(Section));
            }
        }
    }
    return toml::table{
        {"format", 1}, {"regions", std::move(Regions)}, {"roots", std::move(Roots)}, {"panels", std::move(Panels)}};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    NATIVE DOCK RESTORE
//------------------------------------------------------------------------------------------------------------------------

void PanelDockspace::RestoreLayout(float Scale)
{
    const auto &Layout        = PendingLayout;
    const auto &Regions       = *Layout["regions"].as_array();
    const auto *Main          = ImGui::GetMainViewport();
    const bool  NativeEnabled = ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable;
    for (const auto &Panel : *Layout["panels"].as_array())
        for (auto &Entry : Entries)
            if (Entry.Specification.Key == Panel.as_table()->at("key").value<std::string>())
            {
                const auto &Section      = *Panel.as_table();
                Entry.Specification.Open = Section["open"].value_or(false) &&
                                           (!Entry.Specification.Available || Entry.Specification.Available());
                Entry.X                  = static_cast<float>(Section["x"].value_or(64.0));
                Entry.Y                  = static_cast<float>(Section["y"].value_or(64.0));
                Entry.Width              = static_cast<float>(Section["width"].value_or(640.0));
                Entry.Height             = static_cast<float>(Section["height"].value_or(520.0));
                Entry.NativeWindow       = Section["native"].value_or(false) && NativeEnabled;
                if (Entry.NativeWindow)
                {
                    const auto Rectangle =
                        ResolveNativeGeometry({Main->Pos.x + Entry.X * Scale, Main->Pos.y + Entry.Y * Scale,
                                               Entry.Width * Scale, Entry.Height * Scale},
                                              Section);
                    Entry.X      = (Rectangle.X - Main->Pos.x) / Scale;
                    Entry.Y      = (Rectangle.Y - Main->Pos.y) / Scale;
                    Entry.Width  = Rectangle.Width / Scale;
                    Entry.Height = Rectangle.Height / Scale;
                }
                const bool Docked     = Section["docked"].value_or(false);
                Entry.RestoreGeometry = !Docked;
                ImGui::DockBuilderDockWindow(Entry.WindowName.c_str(), Docked ? Dockspace : 0);
                if (Section["native"].value_or(false) && !NativeEnabled)
                    Entry.Placement = -1;
            }
    const auto Restore = [&](auto &&Self, size_t Index, ImGuiID Destination, bool Native, bool ReturnToMain) -> void
    {
        const auto &Section = *Regions[Index].as_table();
        const auto  Axis    = Section["axis"].value_or(std::string{});
        if (Axis != "none")
        {
            ImGuiID First = Destination, Second = Destination;
            if (!ReturnToMain)
                ImGui::DockBuilderSplitNode(Destination, Axis == "x" ? ImGuiDir_Left : ImGuiDir_Up,
                                            static_cast<float>(Section["ratio"].value_or(.5)), &First, &Second);
            Self(Self, static_cast<size_t>(Section["first"].value_or(int64_t{})), First, Native, ReturnToMain);
            Self(Self, static_cast<size_t>(Section["second"].value_or(int64_t{})), Second, Native, ReturnToMain);
        }
        else
        {
            int Order = 0;
            for (const auto &Tab : *Section["tabs"].as_array())
                for (auto &Entry : Entries)
                    if (Entry.Specification.Key == Tab.value<std::string>())
                    {
                        ImGui::DockBuilderDockWindow(Entry.WindowName.c_str(), Destination);
                        Entry.NativeWindow    = Native && NativeEnabled;
                        Entry.RestoreGeometry = false;
                        if (auto *Window = ImGui::FindWindowByName(Entry.WindowName.c_str()))
                            Window->DockOrder = static_cast<short>(Order);
                        else if (auto *Settings = ImGui::FindWindowSettingsByID(ImHashStr(Entry.WindowName.c_str())))
                            Settings->DockOrder = static_cast<short>(Order);
                        ++Order;
                    }
            const auto Selected = Section["selected"].value_or(std::string{});
            for (const auto &Entry : Entries)
                if (Entry.Specification.Key == Selected)
                    SelectedPanels.emplace_back(Destination, Entry.WindowName);
        }
    };
    for (const auto &Root : *Layout["roots"].as_array())
    {
        const auto &Section      = *Root.as_table();
        const bool  Primary      = Section["main"].value_or(false);
        const bool  Native       = Section["native"].value_or(false);
        const bool  ReturnToMain = Native && !NativeEnabled;
        ImGuiID     Destination  = Dockspace;
        if (!Primary && !ReturnToMain)
        {
            Destination          = ImGui::DockBuilderAddNode(0);
            const float Width    = static_cast<float>(Section["width"].value_or(640.0)) * Scale;
            const float Height   = static_cast<float>(Section["height"].value_or(520.0)) * Scale;
            auto        Size     = ImVec2(Native ? Width : std::min(Width, Main->WorkSize.x),
                                          Native ? Height : std::min(Height, Main->WorkSize.y));
            const float X        = Main->Pos.x + static_cast<float>(Section["x"].value_or(64.0)) * Scale;
            const float Y        = Main->Pos.y + static_cast<float>(Section["y"].value_or(64.0)) * Scale;
            auto        Position = ImVec2(
                Native ? X
                       : std::clamp(X, Main->WorkPos.x, Main->WorkPos.x + std::max(0.0f, Main->WorkSize.x - Size.x)),
                Native ? Y
                       : std::clamp(Y, Main->WorkPos.y, Main->WorkPos.y + std::max(0.0f, Main->WorkSize.y - Size.y)));
            if (Native)
            {
                const auto Rectangle = ResolveNativeGeometry({Position.x, Position.y, Size.x, Size.y}, Section);
                Position             = ImVec2(Rectangle.X, Rectangle.Y);
                Size                 = ImVec2(Rectangle.Width, Rectangle.Height);
            }
            ImGui::DockBuilderSetNodePos(Destination, Position);
            ImGui::DockBuilderSetNodeSize(Destination, Size);
        }
        Restore(Restore, static_cast<size_t>(Section["region"].value_or(int64_t{})), Destination, Native, ReturnToMain);
        ImGui::DockBuilderFinish(Destination);
    }
    SelectionFrames = 4;
    PendingLayout.clear();
}

void PanelDockspace::RestoreSelection()
{
    if (SelectionFrames <= 0)
        return;
    --SelectionFrames;
    for (const auto &[Region, Name] : SelectedPanels)
        if (auto *Destination = ImGui::DockBuilderGetNode(Region); Destination && Destination->TabBar)
            if (auto *Window = ImGui::FindWindowByName(Name.c_str()))
                if (auto *Tab = ImGui::TabBarFindTabByID(Destination->TabBar, Window->TabId))
                    ImGui::TabBarQueueFocus(Destination->TabBar, Tab);
    if (!SelectionFrames)
        SelectedPanels.clear();
}
} // namespace Frontier
