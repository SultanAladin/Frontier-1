//============================================================================================================================================
//                                                        GEOMETRYPHASEPROOF.CPP
//============================================================================================================================================
// Headless contract proof for the first Geometry Workspace milestone.

#include "SlateCompute/Compute/GeometryRenderingExchange/Api/GeometryRenderingExchange.h"
#include "SlateCompute/Compute/MaterialProcessingExchange/Api/MaterialProcessingExchange.h"
#include "SlateDocument/Document/GeometryInterchange/Api/GeometryInterchange.h"
#include "SlateDocument/Format/GeometryFormatExchange/Api/GeometryFormatExchange.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Slate;

namespace
{
bool Require(bool Condition, const char* Message)
{
    std::fprintf(stderr, "%s %s\n", Condition ? "[pass]" : "[FAIL]", Message);
    return Condition;
}
}

int main()
{
    bool Passed = true;
    const char* Quad =
        "o ImportedQuad\n"
        "v -1 0 -1\n"
        "v  1 0 -1\n"
        "v  1 0  1\n"
        "v -1 0  1\n"
        "g Surface Collision\n"
        "usemtl White\n"
        "f 1 2 3 4\n"
        "g Surface\n"
        "usemtl Black\n"
        "f 4 3 2 1\n";
    const std::vector<std::uint8_t> Bytes(Quad, Quad + std::char_traits<char>::length(Quad));

    GeometryFormatExchange Formats;
    const GeometryFormatCapability Capability = Formats.Capability("quad.obj");
    Passed &= Require(Capability.ImportSupported && Capability.PolygonFacesRetained &&
                      Capability.NamedObjectsAndGroupsRetained && Capability.MaterialAssignmentsRetained,
                      "GeometryFormatExchange reports faithful OBJ polygon and source-structure import");
    Passed &= Require(!Capability.ExportSupported && !Capability.MaterialDefinitionsRetained,
                      "unsupported export and unavailable external material definitions are reported truthfully");

    const Deliver<DecodedTopology> Decoded = Formats.Decode(Bytes, "quad.obj");
    Passed &= Require(Decoded.Resolved && Decoded.Resolve().Faces.size() == 2u &&
                      Decoded.Resolve().Faces[0].size() == 4u,
                      "the format exchange retains imported quads rather than triangulating them");
    if (!Decoded.Resolved) return 1;
    Passed &= Require(Decoded.Resolve().MaterialNames.size() == 3u &&
                      Decoded.Resolve().MaterialNames[1] == "White" &&
                      Decoded.Resolve().MaterialNames[2] == "Black" &&
                      Decoded.Resolve().MaterialRegistration[0] == 1u &&
                      Decoded.Resolve().MaterialRegistration[1] == 2u,
                      "OBJ material spelling and per-face assignments survive without following mtllib paths");
    Passed &= Require(Decoded.Resolve().ObjectMemberships.size() == 1u &&
                      Decoded.Resolve().ObjectMemberships[0].Faces.size() == 2u &&
                      Decoded.Resolve().GroupMemberships.size() == 2u &&
                      Decoded.Resolve().GroupMemberships[0].Faces.size() == 2u &&
                      Decoded.Resolve().GroupMemberships[1].Faces.size() == 1u,
                      "overlapping OBJ object and group memberships retain exact face ordinals");

    GeometryInterchange Geometry;
    IntakeIndex Intake;
    const Deliver<GeometryIdentity> Registered = Geometry.AcceptDecoded(Decoded.Resolve(), "Imported Quad", Intake);
    Passed &= Require(Registered.Resolved && Geometry.DeclaredCount() == 1u,
                      "GeometryInterchange atomically registers and conditions one geometry asset");
    if (!Registered.Resolved) return 1;
    const Deliver<GeometryAssetView> View = Geometry.Resolve(Registered.Resolve());
    Passed &= Require(View.Resolved && View.Resolve().Topology->FaceCornerCount(0u) == 4u &&
                      View.Resolve().Conditioning->ConditionedRevision() == View.Resolve().Topology->Revision(),
                      "authoritative polygons and derived companions share one revision");
    Passed &= Require(View.Resolved && View.Resolve().SourceRecord != nullptr &&
                      View.Resolve().SourceRecord->MaterialNames.size() == 3u &&
                      View.Resolve().SourceRecord->GroupMemberships.size() == 2u,
                      "GeometryInterchange owns source records for later export and diagnostics");

    GeometryRenderingExchange Rendering;
    const Deliver<GeometryRenderingIdentity> Rendered = Rendering.Synchronise(View.Resolve());
    const Deliver<const GeometryRenderingSnapshot*> Packet =
        Rendered.Resolved ? Rendering.Resolve(Rendered.Resolve())
                           : Deliver<const GeometryRenderingSnapshot*>::Refuse(Rendered.Error);
    Passed &= Require(Packet.Resolved && Packet.Resolve()->Triangles.size() == 4u &&
                      Packet.Resolve()->SourceWire.size() == 4u &&
                      Packet.Resolve()->TriangulatedWire.size() == 5u &&
                      Packet.Resolve()->UnpresentedFaces.empty(),
                      "rendering derives shaded triangles and keeps source and triangulated wire distinct");
    Passed &= Require(Packet.Resolved && Packet.Resolve()->Triangles[0].SourceFace == 0u &&
                      Packet.Resolve()->Triangles[0].MaterialIndex == 1u &&
                      Packet.Resolve()->Triangles[2].SourceFace == 1u &&
                      Packet.Resolve()->Triangles[2].MaterialIndex == 2u,
                      "rendering packets retain triangle-to-face and material mappings");
    Passed &= Require(Rendered.Resolved && Rendering.Retire(Rendered.Resolve()).Resolved &&
                      !Rendering.Resolve(Rendered.Resolve()).Resolved,
                      "disposable rendering identities become stale on retirement");

    const GeometryIdentity RetiredIdentity = Registered.Resolve();
    Passed &= Require(Geometry.Retire(RetiredIdentity).Resolved && !Geometry.Resolve(RetiredIdentity).Resolved,
                      "retirement invalidates stale geometry identities");
    const Deliver<GeometryIdentity> Reused = Geometry.AcceptDecoded(Decoded.Resolve(), "Reused Slot", Intake);
    Passed &= Require(Reused.Resolved && Reused.Resolve().SlotIndex == RetiredIdentity.SlotIndex &&
                      Reused.Resolve().SlotGeneration != RetiredIdentity.SlotGeneration &&
                      !Geometry.Resolve(RetiredIdentity).Resolved,
                      "slot reuse advances its generation without reviving retired identities");
    if (!Reused.Resolved) return 1;
    Geometry.Reclaim();
    const Deliver<GeometryIdentity> AfterReclaim =
        Geometry.AcceptDecoded(Decoded.Resolve(), "After Reclaim", Intake);
    Passed &= Require(AfterReclaim.Resolved && !Geometry.Resolve(Reused.Resolve()).Resolved,
                      "whole-interchange reclamation cannot revive an earlier identity");

    MaterialSpecification Material;
    SurfaceLayerSequence Layers;
    MaterialProcessingExchange Processing;
    const Deliver<LayerIdentity> Base = Processing.InitialiseDielectric(Material, Layers);
    Passed &= Require(Base.Resolved && Layers.EntryCount() == 1u,
                      "every new material receives one base material layer");
    if (!Base.Resolved) return 1;
    const Deliver<const LayerSpecification*> BaseLayer = Layers.Resolve(Base.Resolve());
    Passed &= Require(BaseLayer.Resolved && BaseLayer.Resolve()->Name == "Base Material" &&
                      BaseLayer.Resolve()->Mandatory,
                      "the base layer is named, editable, and mandatory");
    Passed &= Require(std::abs(Material.Channel(ChannelSubject::Metallic).ConstantScalar) < 1.0e-12 &&
                      std::abs(Material.Channel(ChannelSubject::Roughness).ConstantScalar - 0.5) < 1.0e-12 &&
                      Material.Channel(ChannelSubject::AlbedoColour).ConstantColour.RedCoordinate == 1.0,
                      "the initial material is a white dielectric");
    Passed &= Require(!Layers.Withdraw(Base.Resolve()).Resolved,
                      "the mandatory base material layer cannot be removed");
    const MaterialProcessingSnapshot InitialMaterial = Processing.Capture(Material, Layers);
    Passed &= Require(InitialMaterial.Layers.size() == 1u &&
                      Processing.Compare(InitialMaterial, Processing.Capture(Material, Layers)).Empty(),
                      "immutable processing snapshots remain clean while document declarations are unchanged");

    Passed &= Require(Processing.DeclareScalar(Material, Layers, Base.Resolve(),
                                               ChannelSubject::Roughness, 0.2).Resolved &&
                      std::abs(Material.Channel(ChannelSubject::Roughness).ConstantScalar - 0.2) < 1.0e-12,
                      "material constants are edited through the base layer processing seam");
    const MaterialProcessingSnapshot RoughMaterial = Processing.Capture(Material, Layers);
    const MaterialProcessingDirtySet RoughDirty = Processing.Compare(InitialMaterial, RoughMaterial);
    Passed &= Require(RoughDirty.ChannelMask == (1u << static_cast<std::uint32_t>(ChannelSubject::Roughness)) &&
                      !RoughDirty.LayersChanged && !RoughDirty.ReflectanceChanged,
                      "per-channel dirty keys isolate a roughness edit without rebuilding layer structure");

    Passed &= Require(Layers.DeclareName(Base.Resolve(), "Textured Steel").Resolved &&
                      Layers.Resolve(Base.Resolve()).Resolve()->Name == "Textured Steel",
                      "material layer names are editable document data");
    const MaterialProcessingSnapshot NamedMaterial = Processing.Capture(Material, Layers);
    const MaterialProcessingDirtySet NameDirty = Processing.Compare(RoughMaterial, NamedMaterial);
    const MaterialProcessingCapabilities MaterialCapabilities = Processing.Capabilities();
    Passed &= Require(NameDirty.ChannelMask == 0u && NameDirty.LayersChanged &&
                      MaterialCapabilities.ImmutableSnapshots && MaterialCapabilities.ChannelDirtyKeys &&
                      !MaterialCapabilities.LayerSequenceResolution && !MaterialCapabilities.DeviceProcessing,
                      "layer-only dirtiness and unfinished processing capabilities are reported truthfully");

    std::fprintf(stderr, Passed ? "[done] Geometry Phase 2 foundation passed\n"
                                : "[FAIL] Geometry Phase 2 foundation rejected\n");
    return Passed ? 0 : 1;
}
