/*============================================================================================================================================
                                                        COMPONENTCONTENTTRAY.CPP
============================================================================================================================================*/
// 🧩 Implementation of ComponentContentTray matching HtmlReference/05-ContentTray-CollisionChannel.png

#include "Engine/Workspaces/ComponentEditor/ComponentContentTray.h"
#include "Engine/Components/Controls/CapsuleButton.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace Frontier::Workspaces::ComponentEditor
{

ImTextureData* ComponentContentTray::AcquireThumbnailTexture(
    const std::string&               AssetId,
    const ComponentWorkspaceSession& Session) noexcept
{
    const auto It = Session.AssetIcons.find(AssetId);
    if (It == Session.AssetIcons.end() || It->second.Rgba8.empty())
    {
        return nullptr;
    }
    auto& Tex = ThumbnailTextures[AssetId];
    if (Tex.Pixels == nullptr || Tex.Width != static_cast<int>(It->second.Width))
    {
        Tex.DestroyPixels();
        Tex.SetStatus(ImTextureStatus_Destroyed);
        Tex.Create(ImTextureFormat_RGBA32, static_cast<int>(It->second.Width), static_cast<int>(It->second.Height));
        std::memcpy(Tex.GetPixels(), It->second.Rgba8.data(), It->second.Rgba8.size());
    }
    return &Tex;
}

void ComponentContentTray::Render(
    PixelSpace&                Surface,
    Rectangle2D                Bounds,
    ComponentWorkspaceSession& Session,
    const PaneInputReading&    Input) noexcept
{
    const ControlPointer& Ptr = Input.PrimaryPointer;
    const float PadX = 12.0f;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float Y = Bounds.Y + 12.0f;

    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { Bounds.X + PadX, Y }, 17.5f, WorkspaceSurfaceColors::PrimaryInk, "Content");
    char CountBuf[40] {};
    std::snprintf(CountBuf, sizeof(CountBuf), "compiled assets · %zu", Session.Repository.size());
    WorkspaceAppearance::ConstructRightAlignedText(
        Surface, FontWeightClass::Regular, { Bounds.Right() - PadX, Y + 5.0f }, 10.0f, WorkspaceSurfaceColors::MutedInk, CountBuf);
    Y += 32.0f;

    // Import well
    const Rectangle2D ImportWell { Bounds.X + PadX, Y, InnerW, 108.0f };
    Surface.FillRectangle(ImportWell, ColorRgba { 23u, 23u, 26u, 255u }, 12.0f);
    Surface.StrokeRectangle(ImportWell, ColorRgba { 44u, 44u, 48u, 255u }, 12.0f, 1.0f);

    const Rectangle2D ImportBtn { ImportWell.X + 10.0f, ImportWell.Y + 10.0f, ImportWell.Width - 20.0f, 32.0f };
    if (CapsuleButton::Construct(
            Surface, ImportBtn, ButtonDescriptor{ "Import geometry...", ButtonTone::Primary, 11.5f, 14.0f }, Ptr).Clicked)
    {
        const std::filesystem::path SamplePath = Session.RepoRoot / "Experimental/Shared/Models/ControlVehicleBody.facet";
        if (std::filesystem::exists(SamplePath))
        {
            std::string Diag {};
            (void)Session.ImportGeometryFile(SamplePath, Diag);
        }
    }

    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { ImportWell.X + 12.0f, ImportBtn.Bottom() + 8.0f }, ImportWell.Width - 24.0f, 9.0f,
        WorkspaceSurfaceColors::MutedInk, "Runs Geometry Compile (UV, BVH4, LOD, Collision).");

    const std::array<const char*, 7> Fmts {{ "OBJ", "glTF", "GLB", "STL", "PLY", "FCT1", "FBX" }};
    float Fx = ImportWell.X + 10.0f;
    const float Fy = ImportWell.Bottom() - 24.0f;
    for (std::size_t I = 0u; I < Fmts.size(); ++I)
    {
        const float Fw = Surface.MeasureTextWidth(8.6f, Fmts[I]) + 10.0f;
        const Rectangle2D Chip { Fx, Fy, Fw, 16.0f };
        const bool Supported = (I < 6u);
        Surface.FillRectangle(Chip, ColorRgba { 30u, 30u, 34u, static_cast<uint8_t>(Supported ? 255u : 130u) }, 4.0f);
        Surface.TextCentered(Chip, 8.6f, Supported ? WorkspaceSurfaceColors::SecondaryInk : WorkspaceSurfaceColors::FaintInk, Fmts[I]);
        Fx += Fw + 4.0f;
    }
    Y = ImportWell.Bottom() + 12.0f;

    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { Bounds.X + PadX, Y }, 11.2f, WorkspaceSurfaceColors::SecondaryInk, "Compiled");
    Y += 20.0f;

    const Rectangle2D ListRect { Bounds.X, Y, Bounds.Width, std::max(60.0f, Bounds.Bottom() - Y - 8.0f) };
    if (ListRect.Contains({ Ptr.X, Ptr.Y }) && std::abs(Input.WheelDeltaY) > 0.01f)
    {
        ScrollOffsetPx = std::max(0.0f, ScrollOffsetPx - Input.WheelDeltaY * 32.0f);
    }

    const auto* ActiveDoc = Session.InspectActiveDocument();
    Surface.PushClip(ListRect);
    float TileY = ListRect.Y - ScrollOffsetPx;

    for (const auto& Entry : Session.Repository)
    {
        const Rectangle2D Tile { Bounds.X + PadX, TileY, InnerW, 114.0f };
        TileY += 122.0f;
        if (Tile.Bottom() < ListRect.Y || Tile.Y > ListRect.Bottom())
        {
            continue;
        }

        Surface.FillRectangle(Tile, ColorRgba { 22u, 22u, 25u, 255u }, 12.0f);
        Surface.StrokeRectangle(Tile, ColorRgba { 44u, 44u, 48u, 255u }, 12.0f, 1.0f);

        // 48x48 checkerboard + baked 3D thumbnail
        const Rectangle2D Thumb { Tile.X + 10.0f, Tile.Y + 10.0f, 48.0f, 48.0f };
        Surface.FillRectangle(Thumb, WorkspaceSurfaceColors::CheckerDark, 8.0f);
        if (ImTextureData* Tex = AcquireThumbnailTexture(Entry.AssetIdentifier, Session))
        {
            Surface.FillTexturedRectangle(Thumb, Tex, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 8.0f);
        }
        Surface.StrokeRectangle(Thumb, ColorRgba { 52u, 52u, 58u, 255u }, 8.0f, 1.0f);

        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::SemiBold, { Thumb.Right() + 10.0f, Tile.Y + 11.0f }, Tile.Width - 78.0f, 11.2f,
            WorkspaceSurfaceColors::PrimaryInk, Entry.DisplayTitle);

        char Meta[48] {};
        std::snprintf(Meta, sizeof(Meta), "%s · %zu tris", Entry.SourceFormat.c_str(), Entry.Asset.Network.Faces.size());
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { Thumb.Right() + 10.0f, Tile.Y + 26.0f }, Tile.Width - 78.0f, 9.6f,
            WorkspaceSurfaceColors::MutedInk, Meta);

        bool InUse = false;
        if (ActiveDoc != nullptr)
        {
            ActiveDoc->ClassDefinition.Storage.WalkSpatialNesting([&](const ComponentClass::ComponentInstanceHeader& H)
            {
                if (H.Category == ComponentClass::ComponentCategory::GeometryInstance)
                {
                    if (const auto* G = ActiveDoc->ClassDefinition.Storage.InspectGeometry(H.Token);
                        G != nullptr && G->AssetIdentifier == Entry.AssetIdentifier)
                    {
                        InUse = true;
                    }
                }
            });
        }

        // Status chips
        const std::array<const char*, 5> Chips {{ "Compiled", InUse ? "In use" : "Ready", "UV", "BVH4", "LOD" }};
        float Cx = Thumb.Right() + 10.0f;
        const float Cy = Tile.Y + 42.0f;
        for (std::size_t I = 0u; I < Chips.size(); ++I)
        {
            const float Cw = Surface.MeasureTextWidth(8.2f, Chips[I]) + 10.0f;
            if (Cx + Cw > Tile.Right() - 8.0f) break;
            const Rectangle2D CR { Cx, Cy, Cw, 16.0f };
            Surface.FillRectangle(
                CR,
                (I == 0u) ? ColorRgba { 24u, 54u, 36u, 255u } : ColorRgba { 34u, 34u, 38u, 255u },
                4.0f);
            Surface.TextCentered(
                CR, 8.2f,
                (I == 0u) ? ColorRgba { 112u, 224u, 148u, 255u } : WorkspaceSurfaceColors::SecondaryInk,
                Chips[I]);
            Cx += Cw + 4.0f;
        }

        // Bottom 3 action buttons: + Add, New Class, Compile
        const float BtnY = Tile.Bottom() - 32.0f;
        const float BtnW = (Tile.Width - 32.0f) / 3.0f;
        const Rectangle2D B1 { Tile.X + 10.0f, BtnY, BtnW, 24.0f };
        const Rectangle2D B2 { B1.Right() + 6.0f, BtnY, BtnW, 24.0f };
        const Rectangle2D B3 { B2.Right() + 6.0f, BtnY, BtnW, 24.0f };

        if (CapsuleButton::Construct(Surface, B1, ButtonDescriptor{ "+ Add", ButtonTone::Secondary, 10.2f, 8.0f }, Ptr).Clicked)
        {
            Session.AddComponentToActive(
                ComponentClass::ComponentCategory::GeometryInstance,
                Entry.DisplayTitle,
                ComponentClass::PhysicsConstraintCategory::Fixed,
                Entry.AssetIdentifier);
        }
        if (CapsuleButton::Construct(Surface, B2, ButtonDescriptor{ "New Class", ButtonTone::Secondary, 10.2f, 8.0f }, Ptr).Clicked)
        {
            Session.CreateNewClassDocument(Entry.DisplayTitle + "Class", Entry.AssetIdentifier);
        }
        if (CapsuleButton::Construct(Surface, B3, ButtonDescriptor{ "Compile", ButtonTone::Secondary, 10.2f, 8.0f }, Ptr).Clicked)
        {
            Session.BakePendingThumbnails();
        }
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces::ComponentEditor
