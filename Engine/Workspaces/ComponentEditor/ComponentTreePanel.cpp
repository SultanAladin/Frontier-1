/*============================================================================================================================================
                                                         COMPONENTTREEPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of ComponentTreePanel matching HtmlReference/01-Viewport-BPControlVehicle.png left dock

#include "Engine/Workspaces/ComponentEditor/ComponentTreePanel.h"
#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Frontier::Workspaces::ComponentEditor
{

namespace
{

bool ContainsCaseInsensitive(std::string_view Haystack, std::string_view Needle) noexcept
{
    if (Needle.empty()) return true;
    auto It = std::search(
        Haystack.begin(), Haystack.end(),
        Needle.begin(), Needle.end(),
        [](char A, char B) { return std::tolower(static_cast<unsigned char>(A)) == std::tolower(static_cast<unsigned char>(B)); });
    return It != Haystack.end();
}

const char* QueryCategorySubtitle(
    const ComponentClass::ComponentClassDefinition& Def,
    const ComponentClass::ComponentInstanceHeader&  H) noexcept
{
    using Cat = ComponentClass::ComponentCategory;
    switch (H.Category)
    {
        case Cat::SpatialOrigin:   return "SpatialOrigin";
        case Cat::TransformPivot:  return "TransformPivot";
        case Cat::GeometryInstance:
        {
            const auto* G = Def.Storage.InspectGeometry(H.Token);
            return (G != nullptr && !G->AssetIdentifier.empty()) ? G->AssetIdentifier.c_str() : "Geometry";
        }
        case Cat::ConstraintInstance:
        {
            const auto* C = Def.Storage.InspectConstraint(H.Token);
            if (C == nullptr) return "Constraint";
            switch (C->Form)
            {
                case ComponentClass::PhysicsConstraintCategory::Fixed:  return "Fixed constraint";
                case ComponentClass::PhysicsConstraintCategory::Hinge:  return "Hinge constraint";
                case ComponentClass::PhysicsConstraintCategory::Slider: return "Slider constraint";
                case ComponentClass::PhysicsConstraintCategory::Ball:   return "Ball constraint";
                case ComponentClass::PhysicsConstraintCategory::Spring: return "Spring constraint";
                case ComponentClass::PhysicsConstraintCategory::Spin:   return "Spin constraint";
                case ComponentClass::PhysicsConstraintCategory::Tether: return "Tether constraint";
                case ComponentClass::PhysicsConstraintCategory::Dock:   return "Dock constraint";
            }
            return "Constraint";
        }
        case Cat::LightInstance:
        {
            const auto* L = Def.Storage.InspectLight(H.Token);
            return (L != nullptr && L->Form == ComponentClass::LightSourceCategory::SpotBeam) ? "SpotBeam" : "PointEmission";
        }
        case Cat::CameraInstance:   return "Camera";
        case Cat::CollisionVolume:  return "Box trigger";
        case Cat::DirectionArrow:   return "DirectionArrow";
        case Cat::VehicleDynamics:       return "VehicleDynamics";
        case Cat::RotationalMotion:      return "RotationalMotion";
        case Cat::RigidBodyInstance:     return "RigidBody";
        case Cat::SoftBodyInstance:      return "SoftBody";
        case Cat::ForceFieldInstance:    return "ForceField";
        case Cat::FluidRegionInstance:   return "FluidRegion";
        case Cat::ParticleCloudInstance: return "ParticleCloud";
    }
    return "Component";
}

ColorRgba QueryCategoryAccentTint(ComponentClass::ComponentCategory Cat) noexcept
{
    using C = ComponentClass::ComponentCategory;
    switch (Cat)
    {
        case C::GeometryInstance:   return { 222u, 222u, 110u, 255u };
        case C::ConstraintInstance: return { 100u, 182u, 255u, 255u };
        case C::LightInstance:      return { 255u, 210u, 110u, 255u };
        case C::CollisionVolume:    return { 255u, 178u,  56u, 255u };
        case C::RigidBodyInstance:     return { 118u, 196u, 232u, 255u };
        case C::SoftBodyInstance:      return { 172u, 134u, 248u, 255u };
        case C::ForceFieldInstance:    return {  96u, 218u, 208u, 255u };
        case C::FluidRegionInstance:   return { 255u, 146u,  92u, 255u };
        case C::ParticleCloudInstance: return { 255u, 202u,  88u, 255u };
        case C::VehicleDynamics:       return {  88u, 214u, 141u, 255u };
        case C::CameraInstance:     return { 144u, 184u, 255u, 255u };
        default:                    return { 176u, 180u, 190u, 255u };
    }
}

ControlIcon QueryCategoryControlIcon(ComponentClass::ComponentCategory Cat) noexcept
{
    using C = ComponentClass::ComponentCategory;
    switch (Cat)
    {
        case C::SpatialOrigin:      return ControlIcon::PinAnchor;
        case C::TransformPivot:     return ControlIcon::MoveArrows;
        case C::GeometryInstance:   return ControlIcon::CubeShape;
        case C::ConstraintInstance: return ControlIcon::LockClosed;
        case C::LightInstance:      return ControlIcon::SunIllumination;
        case C::CameraInstance:     return ControlIcon::CameraBody;
        case C::CollisionVolume:    return ControlIcon::VolumeBoxes;
        case C::DirectionArrow:     return ControlIcon::MoveArrows;
        case C::VehicleDynamics:    return ControlIcon::GaugeRate;
        case C::RotationalMotion:      return ControlIcon::RefreshCycle;
        case C::RigidBodyInstance:     return ControlIcon::VolumeBoxes;
        case C::SoftBodyInstance:      return ControlIcon::CubeShape;
        case C::ForceFieldInstance:    return ControlIcon::MoveArrows;
        case C::FluidRegionInstance:   return ControlIcon::VolumeBoxes;
        case C::ParticleCloudInstance: return ControlIcon::SunIllumination;
    }
    return ControlIcon::CubeShape;
}

void FormatTriangleCount(uint32_t Tris, char* Out, std::size_t Cap) noexcept
{
    if (Tris >= 1000u)
    {
        std::snprintf(Out, Cap, "%.1fk", static_cast<double>(Tris) / 1000.0);
    }
    else
    {
        std::snprintf(Out, Cap, "%u", Tris);
    }
}

} // namespace

ImTextureData* ComponentTreePanel::AcquireThumbnailTexture(
    const std::string&               AssetId,
    const ComponentWorkspaceSession& Session) noexcept
{
    const auto IconIt = Session.AssetIcons.find(AssetId);
    if (IconIt == Session.AssetIcons.end() || IconIt->second.Rgba8.empty())
    {
        return nullptr;
    }
    auto& Tex = ThumbnailTextures[AssetId];
    if (Tex.Pixels == nullptr || Tex.Width != static_cast<int>(IconIt->second.Width))
    {
        Tex.Create(ImTextureFormat_RGBA32, static_cast<int>(IconIt->second.Width), static_cast<int>(IconIt->second.Height));
        std::memcpy(Tex.GetPixels(), IconIt->second.Rgba8.data(), IconIt->second.Rgba8.size());
        Tex.SetStatus(ImTextureStatus_WantCreate);
    }
    return &Tex;
}

void ComponentTreePanel::Render(
    PixelSpace&                Surface,
    Rectangle2D                Bounds,
    ComponentWorkspaceSession& Session,
    const PaneInputReading&    Input,
    double                     DeltaSeconds) noexcept
{
    auto* Doc = Session.AccessActiveDocument();
    if (Doc == nullptr)
    {
        return;
    }

    const ControlPointer& Ptr = Input.PrimaryPointer;
    const auto Metrics = Doc->ClassDefinition.Storage.ComputeSummaryMetrics();

    uint32_t TotalTriangles = 0u;
    Doc->ClassDefinition.Storage.WalkSpatialNesting([&](const ComponentClass::ComponentInstanceHeader& H)
    {
        if (H.Category == ComponentClass::ComponentCategory::GeometryInstance)
        {
            if (const auto* G = Doc->ClassDefinition.Storage.InspectGeometry(H.Token))
            {
                for (const auto& Entry : Session.Repository)
                {
                    if (Entry.AssetIdentifier == G->AssetIdentifier)
                    {
                        TotalTriangles += static_cast<uint32_t>(Entry.Asset.Network.Faces.size());
                        break;
                    }
                }
            }
        }
    });

    // 1. Header: "Components" + active class title
    float Y = Bounds.Y + 12.0f;
    const float PadX = 12.0f;
    const float InnerW = Bounds.Width - PadX * 2.0f;

    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { Bounds.X + PadX, Y }, 17.5f, WorkspaceSurfaceColors::PrimaryInk, "Components");
    WorkspaceAppearance::ConstructRightAlignedText(
        Surface, FontWeightClass::Regular, { Bounds.Right() - PadX, Y + 5.0f }, 10.2f, WorkspaceSurfaceColors::MutedInk,
        Doc->ClassDefinition.Title);
    Y += 32.0f;

    // 2. Two metric tiles: Components & Triangles
    const float TileW = (InnerW - 8.0f) * 0.5f;
    const float TileH = 54.0f;
    auto ConstructMetricTile = [&](Rectangle2D R, const char* Label, const char* MetricStr, ColorRgba DotTint)
    {
        Surface.FillRectangle(R, ColorRgba { 24u, 24u, 27u, 255u }, 12.0f);
        Surface.StrokeRectangle(R, ColorRgba { 44u, 44u, 48u, 255u }, 12.0f, 1.0f);
        Surface.FillCircle({ R.X + 16.0f, R.Y + 17.0f }, 6.5f, DotTint);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { R.X + 10.0f, R.Y + 32.0f }, 9.8f, WorkspaceSurfaceColors::MutedInk, Label);
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface, FontWeightClass::SemiBold, { R.Right() - 12.0f, R.Y + 26.0f }, 16.0f, WorkspaceSurfaceColors::PrimaryInk, MetricStr);
    };

    char CntBuf[16] {};
    char TriBuf[16] {};
    std::snprintf(CntBuf, sizeof(CntBuf), "%u", Metrics.TotalComponents);
    FormatTriangleCount(TotalTriangles, TriBuf, sizeof(TriBuf));
    ConstructMetricTile({ Bounds.X + PadX, Y, TileW, TileH }, "Components", CntBuf, { 46u, 204u, 113u, 255u });
    ConstructMetricTile({ Bounds.X + PadX + TileW + 8.0f, Y, TileW, TileH }, "Triangles", TriBuf, { 112u, 116u, 128u, 255u });
    Y += TileH + 10.0f;

    // 3. Cream "+ Add" capsule + Search components field
    const Rectangle2D AddBtnRect { Bounds.X + PadX, Y, 66.0f, 30.0f };
    if (CapsuleButton::Construct(
            Surface, AddBtnRect, ButtonDescriptor{ "+ Add", ButtonTone::Primary, 11.2f, 12.0f }, Ptr).Clicked)
    {
        Session.AddSheetOpen = !Session.AddSheetOpen;
    }

    const Rectangle2D SearchRect { AddBtnRect.Right() + 8.0f, Y, Bounds.Right() - PadX - (AddBtnRect.Right() + 8.0f), 30.0f };
    Surface.FillRectangle(SearchRect, WorkspaceSurfaceColors::ControlWell, 15.0f);
    Surface.StrokeRectangle(SearchRect, WorkspaceSurfaceColors::ControlWellBorder, 15.0f, 1.0f);
    if (ControlAppearance::QueryPointerOver(SearchRect, Ptr) && Ptr.Pressed && !Session.ComponentSearch.Active)
    {
        Session.ComponentSearch.Begin(Session.ComponentSearch.Text);
    }
    if (Session.ComponentSearch.Active)
    {
        WorkspaceAppearance::ForwardInputToTextBuffer(Session.ComponentSearch, Input, DeltaSeconds);
        (void)TextInputField::Construct(Surface, SearchRect.InsetXY(8.0f, 3.0f), Session.ComponentSearch, Ptr, 11.0f);
    }
    else if (Session.ComponentSearch.Length == 0u)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SearchRect.X + 12.0f, SearchRect.Y + 8.0f }, 10.2f,
            WorkspaceSurfaceColors::FaintInk, "Search components");
    }
    else
    {
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { SearchRect.X + 12.0f, SearchRect.Y + 8.0f }, SearchRect.Width - 20.0f, 10.2f,
            WorkspaceSurfaceColors::PrimaryInk, Session.ComponentSearch.Text);
    }
    Y += 38.0f;

    // 4. Footer reservation
    const float FooterH = 38.0f;
    const Rectangle2D FooterRect { Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    const Rectangle2D ListRect { Bounds.X + 4.0f, Y, Bounds.Width - 8.0f, std::max(60.0f, FooterRect.Y - Y - 4.0f) };

    if (ListRect.Contains({ Ptr.X, Ptr.Y }) && std::abs(Input.WheelDeltaY) > 0.01f)
    {
        ScrollOffsetPx = std::max(0.0f, ScrollOffsetPx - Input.WheelDeltaY * 28.0f);
    }

    // 5. Hierarchical component rows + Behaviours
    Surface.PushClip(ListRect);
    float RowY = ListRect.Y - ScrollOffsetPx;
    constexpr float RowH = 36.0f;

    const std::string_view Filter = Session.ComponentSearch.Text;
    std::vector<const ComponentClass::ComponentInstanceHeader*> VisibleRows {};
    Doc->ClassDefinition.Storage.WalkSpatialNesting([&](const ComponentClass::ComponentInstanceHeader& H)
    {
        if (!Filter.empty() && !ContainsCaseInsensitive(H.Title, Filter))
        {
            return;
        }
        // Check if any ancestor is collapsed
        if (Filter.empty() && H.EnclosingIndex != ComponentClass::InvalidNestingIndex)
        {
            uint32_t AncIdx = H.EnclosingIndex;
            while (AncIdx != ComponentClass::InvalidNestingIndex)
            {
                const auto* Anc = Doc->ClassDefinition.Storage.InspectHeaderByIndex(AncIdx);
                if (Anc == nullptr) break;
                if (CollapsedNodes[Anc->Token.SlotIndex])
                {
                    return;
                }
                AncIdx = Anc->EnclosingIndex;
            }
        }
        VisibleRows.push_back(&H);
    });

    for (const auto* HPtr : VisibleRows)
    {
        const auto& H = *HPtr;
        const Rectangle2D RowRect { ListRect.X + 4.0f, RowY, ListRect.Width - 8.0f, RowH - 2.0f };
        RowY += RowH;
        if (RowRect.Bottom() < ListRect.Y || RowRect.Y > ListRect.Bottom())
        {
            continue;
        }

        const bool Selected = (H.Token == Doc->SelectedToken);
        const bool Hovered  = ControlAppearance::QueryPointerOver(RowRect, Ptr);

        if (Selected)
        {
            Surface.FillRectangle(RowRect, ColorRgba { 30u, 30u, 33u, 255u }, 8.0f);
            Surface.StrokeRectangle(RowRect, ColorRgba { 164u, 114u, 48u, 220u }, 8.0f, 1.2f);
            Surface.FillRectangle({ RowRect.X + 2.0f, RowRect.Y + 6.0f, 3.0f, RowRect.Height - 12.0f }, WorkspaceSurfaceColors::SelectionAmber, 1.5f);
        }
        else if (Hovered)
        {
            Surface.FillRectangle(RowRect, WorkspaceSurfaceColors::RowHover, 8.0f);
        }

        const float IndentX = RowRect.X + 10.0f + static_cast<float>(H.DepthLevel) * 14.0f;
        const bool NestedPresent = (H.FirstNestedIndex != ComponentClass::InvalidNestingIndex);
        if (NestedPresent)
        {
            const Rectangle2D ChevRect { IndentX - 4.0f, RowRect.Y + 8.0f, 14.0f, 18.0f };
            const bool Collapsed = CollapsedNodes[H.Token.SlotIndex];
            WorkspaceAppearance::ConstructChevronSymbol(
                Surface, ChevRect.Center(), 6.0f, !Collapsed, WorkspaceSurfaceColors::MutedInk);
            if (ControlAppearance::QueryPointerOver(ChevRect, Ptr) && Ptr.Pressed)
            {
                CollapsedNodes[H.Token.SlotIndex] = !Collapsed;
            }
        }

        // Category icon or baked geometry mini thumbnail
        const Rectangle2D IconRect { IndentX + 13.0f, RowRect.Center().Y - 9.0f, 18.0f, 18.0f };
        bool DrewThumb = false;
        if (H.Category == ComponentClass::ComponentCategory::GeometryInstance)
        {
            if (const auto* G = Doc->ClassDefinition.Storage.InspectGeometry(H.Token))
            {
                if (ImTextureData* Tex = AcquireThumbnailTexture(G->AssetIdentifier, Session))
                {
                    Surface.FillTexturedRectangle(IconRect, Tex, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 3.0f);
                    DrewThumb = true;
                }
            }
        }
        if (!DrewThumb)
        {
            VectorIconRepository::ConstructControl(
                Surface, QueryCategoryControlIcon(H.Category),
                { IconRect.X + 2.0f, IconRect.Y + 2.0f, 14.0f, QueryCategoryAccentTint(H.Category), 1.6f });
        }

        const float TextX = IconRect.Right() + 7.0f;
        const float EyeX  = RowRect.Right() - 22.0f;
        const float TextW = std::max(24.0f, EyeX - TextX - 4.0f);

        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            Selected ? FontWeightClass::SemiBold : FontWeightClass::Medium,
            { TextX, RowRect.Y + 4.0f },
            TextW,
            10.8f,
            Selected ? WorkspaceSurfaceColors::PrimaryInk : ColorRgba { 216u, 218u, 224u, 255u },
            H.Title);

        const char* Sub = QueryCategorySubtitle(Doc->ClassDefinition, H);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { TextX, RowRect.Y + 18.0f }, TextW, 9.0f, WorkspaceSurfaceColors::FaintInk, Sub);

        if (H.Category == ComponentClass::ComponentCategory::SpatialOrigin)
        {
            const float TitleW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.8f, H.Title);
            const Rectangle2D RootTag { TextX + TitleW + 6.0f, RowRect.Y + 4.0f, 34.0f, 14.0f };
            if (RootTag.Right() < EyeX)
            {
                Surface.FillRectangle(RootTag, ColorRgba { 38u, 40u, 44u, 255u }, 4.0f);
                Surface.TextCentered(RootTag, 8.2f, WorkspaceSurfaceColors::MutedInk, "ROOT");
            }
        }

        // Visibility eye toggle
        if (H.Category != ComponentClass::ComponentCategory::SpatialOrigin)
        {
            const Rectangle2D EyeRect { EyeX, RowRect.Y + 7.0f, 18.0f, 20.0f };
            WorkspaceAppearance::ConstructVisibilityIndicator(Surface, EyeRect.Center(), H.VisibilityEnabled, 0.9f);
            if (ControlAppearance::QueryPointerOver(EyeRect, Ptr) && Ptr.Pressed)
            {
                if (auto* MutH = Doc->ClassDefinition.Storage.ResolveHeader(H.Token))
                {
                    MutH->VisibilityEnabled = !MutH->VisibilityEnabled;
                    Session.MarkActiveDirty();
                }
            }
        }

        // Selection & drag-reparent
        if (Hovered && Ptr.Pressed && !Session.AddSheetOpen)
        {
            Doc->SelectedToken = H.Token;
            if (H.Category != ComponentClass::ComponentCategory::SpatialOrigin)
            {
                DraggedToken = H.Token;
            }
            if (Input.DoubleClicked)
            {
                Session.FocusSelectedOrClass();
            }
        }
        if (DraggedToken.Valid() && Ptr.Released && Hovered && DraggedToken != H.Token)
        {
            if (Doc->ClassDefinition.Storage.AttachInstance(DraggedToken, H.Token))
            {
                Session.MarkActiveDirty();
            }
            DraggedToken = {};
        }
    }

    // Behaviours section
    std::vector<ComponentClass::ComponentInstanceToken> Behaviours {};
    Doc->ClassDefinition.Storage.CollectBehaviourTokens(Behaviours);
    if (!Behaviours.empty())
    {
        RowY += 8.0f;
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { ListRect.X + 12.0f, RowY }, 9.4f, WorkspaceSurfaceColors::FaintInk, "BEHAVIOURS");
        RowY += 18.0f;
        for (const auto BTok : Behaviours)
        {
            const auto* BH = Doc->ClassDefinition.Storage.InspectHeader(BTok);
            if (BH == nullptr || (!Filter.empty() && !ContainsCaseInsensitive(BH->Title, Filter))) continue;
            const Rectangle2D RowRect { ListRect.X + 4.0f, RowY, ListRect.Width - 8.0f, RowH - 2.0f };
            RowY += RowH;
            const bool Selected = (BTok == Doc->SelectedToken);
            if (Selected)
            {
                Surface.FillRectangle(RowRect, ColorRgba { 30u, 30u, 33u, 255u }, 8.0f);
                Surface.StrokeRectangle(RowRect, ColorRgba { 164u, 114u, 48u, 220u }, 8.0f, 1.2f);
            }
            VectorIconRepository::ConstructControl(
                Surface, QueryCategoryControlIcon(BH->Category),
                { RowRect.X + 11.0f, RowRect.Center().Y - 7.0f, 14.0f, QueryCategoryAccentTint(BH->Category), 1.6f });
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Medium, { RowRect.X + 30.0f, RowRect.Y + 4.0f }, RowRect.Width - 40.0f, 10.8f,
                WorkspaceSurfaceColors::PrimaryInk, BH->Title);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { RowRect.X + 30.0f, RowRect.Y + 18.0f }, RowRect.Width - 40.0f, 9.0f,
                WorkspaceSurfaceColors::FaintInk, QueryCategorySubtitle(Doc->ClassDefinition, *BH));
            if (ControlAppearance::QueryPointerOver(RowRect, Ptr) && Ptr.Pressed)
            {
                Doc->SelectedToken = BTok;
            }
        }
    }

    if (Ptr.Released)
    {
        DraggedToken = {};
    }
    Surface.PopClip();

    // 6. "+ Add" floating grouped directive sheet via DropdownSelector
    if (Session.AddSheetOpen)
    {
        struct AddDirectiveSpec
        {
            const char*                           Label;
            ComponentClass::ComponentCategory     Cat;
            ComponentClass::PhysicsConstraintCategory ConCategory;
            bool                                  HeaderOnly;
        };
        static constexpr std::array<AddDirectiveSpec, 15> Specs {{
            { "SCENE & CONSTRAINTS", ComponentClass::ComponentCategory::SpatialOrigin,         ComponentClass::PhysicsConstraintCategory::Fixed,  true  },
            { "Geometry Instance",   ComponentClass::ComponentCategory::GeometryInstance,      ComponentClass::PhysicsConstraintCategory::Fixed,  false },
            { "Spring Constraint",   ComponentClass::ComponentCategory::ConstraintInstance,    ComponentClass::PhysicsConstraintCategory::Spring, false },
            { "Hinge Constraint",    ComponentClass::ComponentCategory::ConstraintInstance,    ComponentClass::PhysicsConstraintCategory::Hinge,  false },
            { "Dock Constraint",     ComponentClass::ComponentCategory::ConstraintInstance,    ComponentClass::PhysicsConstraintCategory::Dock,   false },
            { "SpotBeam Light",      ComponentClass::ComponentCategory::LightInstance,         ComponentClass::PhysicsConstraintCategory::Fixed,  false },
            { "Box Collider",        ComponentClass::ComponentCategory::CollisionVolume,       ComponentClass::PhysicsConstraintCategory::Fixed,  false },
            { "Camera",              ComponentClass::ComponentCategory::CameraInstance,        ComponentClass::PhysicsConstraintCategory::Fixed,  false },
            { "SIMULATION",          ComponentClass::ComponentCategory::SoftBodyInstance,      ComponentClass::PhysicsConstraintCategory::Fixed,  true  },
            { "Soft Body",           ComponentClass::ComponentCategory::SoftBodyInstance,      ComponentClass::PhysicsConstraintCategory::Fixed,  false },
            { "Force Field",         ComponentClass::ComponentCategory::ForceFieldInstance,    ComponentClass::PhysicsConstraintCategory::Fixed,  false },
            { "Fluid Region",        ComponentClass::ComponentCategory::FluidRegionInstance,   ComponentClass::PhysicsConstraintCategory::Fixed,  false },
            { "Particle Cloud",      ComponentClass::ComponentCategory::ParticleCloudInstance, ComponentClass::PhysicsConstraintCategory::Fixed,  false },
            { "BEHAVIOURS",          ComponentClass::ComponentCategory::VehicleDynamics,       ComponentClass::PhysicsConstraintCategory::Fixed,  true  },
            { "Vehicle Dynamics",    ComponentClass::ComponentCategory::VehicleDynamics,       ComponentClass::PhysicsConstraintCategory::Fixed,  false },
        }};

        std::array<DropdownDirectiveEntry, 15> Entries {};
        for (std::size_t I = 0u; I < Specs.size(); ++I)
        {
            Entries[I].Label          = Specs[I].Label;
            Entries[I].SectionCaption = Specs[I].HeaderOnly;
            Entries[I].DividerAbove   = (I == 8u || I == 13u);
            if (!Specs[I].HeaderOnly)
            {
                Entries[I].LeadingTint = QueryCategoryAccentTint(Specs[I].Cat);
                Entries[I].LeadingIcon = QueryCategoryControlIcon(Specs[I].Cat);
            }
        }

        const Rectangle2D SheetBounds = DropdownSelector::QueryDirectiveSheetBounds(
            { Bounds.X + PadX, AddBtnRect.Bottom() + 4.0f }, Bounds.Width - PadX * 2.0f, Entries);
        int32_t Chosen = -1;
        if (DropdownSelector::ConstructDirectiveSheet(Surface, SheetBounds, Entries, Ptr, Chosen, 1.0f).Clicked &&
            Chosen >= 0 && Chosen < static_cast<int32_t>(Specs.size()) && !Specs[Chosen].HeaderOnly)
        {
            Session.AddComponentToActive(Specs[Chosen].Cat, Specs[Chosen].Label, Specs[Chosen].ConCategory);
            Session.AddSheetOpen = false;
        }
        else if (Ptr.Pressed && !SheetBounds.Contains({ Ptr.X, Ptr.Y }) && !AddBtnRect.Contains({ Ptr.X, Ptr.Y }))
        {
            Session.AddSheetOpen = false;
        }
    }

    // 7. 3-hint footer (3 aligned columns)
    Surface.FillRectangle(FooterRect, WorkspaceSurfaceColors::FooterBackground, 0.0f);
    Surface.StrokeLine({ FooterRect.X, FooterRect.Y }, { FooterRect.Right(), FooterRect.Y }, WorkspaceSurfaceColors::HairlineLight, 1.0f);
    struct HintCol { float OffsetX; const char* Key; const char* Desc; };
    static constexpr std::array<HintCol, 3> Hints {{
        { 12.0f,  "DRAG",       "re-parent" },
        { 86.0f,  "DBL-CLICK",  "focus"     },
        { 168.0f, "DEL · CTRL D", "remove · copy" },
    }};
    for (const auto& H : Hints)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { FooterRect.X + H.OffsetX, FooterRect.Y + 7.0f }, 8.4f, WorkspaceSurfaceColors::FaintInk, H.Key);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { FooterRect.X + H.OffsetX, FooterRect.Y + 20.0f }, 9.0f, WorkspaceSurfaceColors::SecondaryInk, H.Desc);
    }
}

} // namespace Frontier::Workspaces::ComponentEditor
