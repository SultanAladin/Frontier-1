//============================================================================================================================================
//                                                        CODEXSCENEPROXY.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/CodexSceneProxy/Api/CodexSceneProxy.h"

#include "SlateWorkspace/Discipline/ViewportProjection/Api/SketchBasis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Slate
{

// 📝 A codex records positions in metres; the sketch works in millimetres. One constant, stated once.

EntitySubject SceneEntrySubject(CodexSceneSubject Subject)
{
    switch (Subject)
    {
        case CodexSceneSubject::Sun:        return EntitySubject::Sun;
        case CodexSceneSubject::Sky:        return EntitySubject::Sky;
        case CodexSceneSubject::Atmosphere: return EntitySubject::Actor;
        case CodexSceneSubject::Geometry:   return EntitySubject::Actor;
        default:                            return EntitySubject::Actor;
    }
}

const char* SceneEntryTag(const CodexSceneEntry& Entry)
{
    switch (Entry.Subject)
    {
        case CodexSceneSubject::Sun:        return "codex reference sun light";
        case CodexSceneSubject::Sky:        return "codex reference sky atmosphere";
        case CodexSceneSubject::Atmosphere: return "codex reference atmosphere settings";
        case CodexSceneSubject::Geometry:   return Entry.MaterialReference.c_str();
        default:                            return "codex reference";
    }
}

void AppendSceneDirectoryRow(SceneDirectoryRows& Storage,
                                 const char* Naming,
                                 EntitySubject Subject,
                                 std::uint32_t Depth,
                                 std::uint32_t Enclosing,
                                 std::uint32_t EnclosedCount,
                                 const char* Tagged,
                                 StableRowIdentity Identity)
{
    if (Storage.RowCount >= SceneDirectoryContext::EntityLimit)
        return;

    const std::uint32_t RowIndex = Storage.RowCount++;
    std::snprintf(Storage.Names[RowIndex], sizeof(Storage.Names[RowIndex]), "%s", Naming != nullptr ? Naming : "");
    std::snprintf(Storage.Tags[RowIndex], sizeof(Storage.Tags[RowIndex]), "%s", Tagged != nullptr ? Tagged : "");

    EntityRow& Row = Storage.Rows[RowIndex];
    Row = {};
    Row.Naming = Storage.Names[RowIndex];
    Row.Subject = Subject;
    Row.Depth = Depth;
    Row.Enclosing = Enclosing;
    Row.EnclosedCount = EnclosedCount;
    Row.Tagged = Storage.Tags[RowIndex];
    Row.Camera = CameraRole::Absent;
    Row.Identity = Identity;
}

std::uint32_t BuildSceneDirectoryRows(const WorkspaceCodex& Workspace,
                                                SceneDirectoryRows& Storage)
{
    Storage = SceneDirectoryRows{};

    std::uint32_t LightingCount = 0u;
    std::uint32_t GeometryCount = 0u;
    for (const CodexSceneEntry& Entry : Workspace.Scene)
    {
        if (Entry.Subject == CodexSceneSubject::Geometry)
            ++GeometryCount;
        else
            ++LightingCount;
    }

    AppendSceneDirectoryRow(Storage,
                         Workspace.Naming.empty() ? "Workspace Scene" : Workspace.Naming.c_str(),
                         EntitySubject::Level, 0u, 0xFFFFFFFFu,
                         (LightingCount > 0u ? 1u : 0u) + (GeometryCount > 0u ? 1u : 0u),
                         "workspace codex scene", 6000u);

    std::uint32_t LightingRow = 0xFFFFFFFFu;
    if (LightingCount > 0u)
    {
        LightingRow = Storage.RowCount;
        AppendSceneDirectoryRow(Storage, "Lighting", EntitySubject::Grouping, 1u, 0u,
                             LightingCount, "codex lighting folder", 6001u);
        for (std::uint32_t Index = 0u; Index < Workspace.Scene.size(); ++Index)
        {
            const CodexSceneEntry& Entry = Workspace.Scene[Index];
            if (Entry.Subject == CodexSceneSubject::Geometry)
                continue;
            AppendSceneDirectoryRow(Storage, Entry.Naming.c_str(), SceneEntrySubject(Entry.Subject), 2u,
                                 LightingRow, 0u, SceneEntryTag(Entry), 6100u + Index);
        }
    }

    std::uint32_t ObjectsRow = 0xFFFFFFFFu;
    if (GeometryCount > 0u)
    {
        ObjectsRow = Storage.RowCount;
        AppendSceneDirectoryRow(Storage, "Scene Objects", EntitySubject::Grouping, 1u, 0u,
                             GeometryCount, "codex geometry folder", 6002u);
        for (std::uint32_t Index = 0u; Index < Workspace.Scene.size(); ++Index)
        {
            const CodexSceneEntry& Entry = Workspace.Scene[Index];
            if (Entry.Subject != CodexSceneSubject::Geometry)
                continue;
            AppendSceneDirectoryRow(Storage, Entry.Naming.c_str(), EntitySubject::Actor, 2u,
                                 ObjectsRow, 0u, SceneEntryTag(Entry), 6200u + Index);
        }
    }

    return Storage.RowCount;
}

bool CadReferenced(WorkspaceRecordSubject Subject)
{
    return Subject == WorkspaceRecordSubject::ClosedProfile ||
           Subject == WorkspaceRecordSubject::ThinSurface ||
           Subject == WorkspaceRecordSubject::Solid;
}

void AppendCadReferenceRows(const WorkspaceRecordStructure& Records,
                                      SceneDirectoryRows& Storage)
{
    std::uint32_t ReferenceCount = 0u;
    for (std::uint32_t Index = 1u; Index <= Records.DeclaredCount(); ++Index)
    {
        const WorkspaceRecord* Record = Records.Resolve({ Index });
        if (Record != nullptr && Record->Visible && CadReferenced(Record->Subject))
            ++ReferenceCount;
    }
    if (ReferenceCount == 0u)
        return;

    if (Storage.RowCount == 0u)
    {
        AppendSceneDirectoryRow(Storage, "Sketch Scene", EntitySubject::Level, 0u, 0xFFFFFFFFu,
                             0u, "sketch scene", 6000u);
    }

    Storage.Rows[0u].EnclosedCount += 1u;
    const std::uint32_t Folder = Storage.RowCount;
    AppendSceneDirectoryRow(Storage, "CAD References", EntitySubject::Grouping, 1u, 0u,
                         ReferenceCount, "cad references folder", 6003u);

    for (std::uint32_t Index = 1u; Index <= Records.DeclaredCount(); ++Index)
    {
        const WorkspaceRecord* Record = Records.Resolve({ Index });
        if (Record == nullptr || !Record->Visible || !CadReferenced(Record->Subject))
            continue;

        const char* Tag = Record->Subject == WorkspaceRecordSubject::ClosedProfile ? "CAD reference closed profile" :
                          Record->Subject == WorkspaceRecordSubject::ThinSurface ? "CAD reference thin surface" :
                                                                                   "CAD reference solid";
        AppendSceneDirectoryRow(Storage, Record->Naming.c_str(), EntitySubject::Actor, 2u, Folder,
                             0u, Tag, 7000u + Index);
    }
}

void ApplySceneEnvironment(const WorkspaceCodex& Workspace,
                                        SceneDirectoryContext& Applied)
{
    Applied.Environment.SunElevation = Workspace.Environment.SunElevation;
    Applied.Environment.SunAzimuth = Workspace.Environment.SunAzimuth;
    Applied.Environment.SunIntensity = Workspace.Environment.SunIntensity;
    Applied.Environment.SunTemperature = Workspace.Environment.SunTemperature;
    Applied.Environment.SkyIntensity = Workspace.Environment.SkyIntensity;
    Applied.Environment.AtmosphereDensity = Workspace.Environment.AtmosphereDensity;
    Applied.Environment.AtmosphereScaleHeight = Workspace.Environment.AtmosphereScaleHeight;
}

SpatialPoint CodexScenePosition(const CodexSceneEntry& Entry, double UnitScale)
{
    return { Entry.Position[0] * UnitScale,
             Entry.Position[1] * UnitScale,
             Entry.Position[2] * UnitScale };
}

void ResolveCodexProxyExtent(const CodexSceneEntry& Entry,
                             double& HalfX,
                             double& HalfY,
                             double& HalfZ)
{
    const char* Name = Entry.Naming.c_str();
    if (std::strstr(Name, "Teapot") != nullptr)
    {
        HalfX = 150.0; HalfY = 85.0; HalfZ = 105.0;
    }
    else if (std::strstr(Name, "Teacup") != nullptr)
    {
        HalfX = 62.0; HalfY = 48.0; HalfZ = 62.0;
    }
    else if (std::strstr(Name, "Saucer") != nullptr)
    {
        HalfX = 88.0; HalfY = 10.0; HalfZ = 88.0;
    }
    else if (std::strstr(Name, "Sugar") != nullptr)
    {
        HalfX = 78.0; HalfY = 58.0; HalfZ = 72.0;
    }
    else if (std::strstr(Name, "Milk") != nullptr)
    {
        HalfX = 66.0; HalfY = 72.0; HalfZ = 54.0;
    }
    else if (std::strstr(Name, "Floor") != nullptr)
    {
        HalfX = 1000.0 * Entry.Scale[0]; HalfY = 1.0; HalfZ = 1000.0 * Entry.Scale[2];
    }
    else
    {
        HalfX = 60.0; HalfY = 60.0; HalfZ = 60.0;
    }
}

bool ResolveSelectedSceneMeshPivot(const WorkspaceCodex& Scene,
                                   bool SceneStanding,
                                   const SceneDirectoryRows& Storage,
                                   const SceneDirectoryContext& Applied,
                                   double UnitScale,
                                   SpatialPoint& Pivot)
{
    if (!SceneStanding || Applied.EntityTaken >= Storage.RowCount || Applied.EntityTaken >= SceneDirectoryContext::EntityLimit)
        return false;
    const StableRowIdentity Identity = Storage.Rows[Applied.EntityTaken].Identity;
    if (Identity < 6200u)
        return false;
    const std::uint32_t SceneIndex = static_cast<std::uint32_t>(Identity - 6200u);
    if (SceneIndex >= Scene.Scene.size() || Scene.Scene[SceneIndex].Subject != CodexSceneSubject::Geometry)
        return false;
    Pivot = CodexScenePosition(Scene.Scene[SceneIndex], UnitScale);
    return true;
}

bool SelectSceneMeshAtPointer(const PlaneExtent& Extent,
                              const PointerCondition& Pointer,
                              const ResolvedCamera& Camera,
                              const WorkspaceCodex& Scene,
                              bool SceneStanding,
                              const SceneDirectoryRows& Storage,
                              double UnitScale,
                              SceneDirectoryContext& Applied)
{
    if (!SceneStanding || !Pointer.ContactPressed || !Extent.Encloses(Pointer.PositionX, Pointer.PositionY))
        return false;

    std::uint32_t BestRow = SceneDirectoryContext::EntityLimit;
    double BestArea = 1.0e300;
    for (std::uint32_t Row = 0u; Row < Storage.RowCount && Row < SceneDirectoryContext::EntityLimit; ++Row)
    {
        const StableRowIdentity Identity = Storage.Rows[Row].Identity;
        if (Identity < 6200u)
            continue;
        const std::uint32_t SceneIndex = static_cast<std::uint32_t>(Identity - 6200u);
        if (SceneIndex >= Scene.Scene.size())
            continue;
        const CodexSceneEntry& Entry = Scene.Scene[SceneIndex];
        const CodexSceneMesh* Mesh = nullptr;
        for (const CodexSceneMesh& Candidate : Scene.SceneMeshes)
            if (Candidate.Naming == Entry.GeometryReference)
            {
                Mesh = &Candidate;
                break;
            }
        if (Mesh == nullptr || Mesh->Positions.size() < 3u)
            continue;

        const SpatialPoint Centre = CodexScenePosition(Entry, UnitScale);
        float MinX = 1.0e30f, MinY = 1.0e30f, MaxX = -1.0e30f, MaxY = -1.0e30f;
        for (std::size_t Vertex = 0u; Vertex * 3u + 2u < Mesh->Positions.size(); ++Vertex)
        {
            float X = 0.0f, Y = 0.0f;
            if (!ProjectOffsetFromCamera(Camera, Extent, Centre,
                Mesh->Positions[Vertex * 3u + 0u] * Entry.Scale[0] * UnitScale,
                Mesh->Positions[Vertex * 3u + 1u] * Entry.Scale[1] * UnitScale,
                Mesh->Positions[Vertex * 3u + 2u] * Entry.Scale[2] * UnitScale, X, Y))
                continue;
            MinX = std::min(MinX, X); MinY = std::min(MinY, Y);
            MaxX = std::max(MaxX, X); MaxY = std::max(MaxY, Y);
        }
        if (MinX > MaxX || MinY > MaxY)
            continue;
        const float Pad = 8.0f;
        if (Pointer.PositionX + Pad < MinX || Pointer.PositionX - Pad > MaxX ||
            Pointer.PositionY + Pad < MinY || Pointer.PositionY - Pad > MaxY)
            continue;
        const double Area = static_cast<double>(MaxX - MinX) * static_cast<double>(MaxY - MinY);
        if (Area < BestArea)
        {
            BestArea = Area;
            BestRow = Row;
        }
    }

    if (BestRow >= SceneDirectoryContext::EntityLimit)
        return false;
    for (bool& Selected : Applied.EntitySelected)
        Selected = false;
    Applied.EntitySelected[BestRow] = true;
    Applied.EntityTaken = BestRow;
    Applied.EntitySelectionAnchor = BestRow;
    return true;
}

ThemeToken CodexMaterialToken(const WorkspaceCodex& Scene,
                              const CodexSceneEntry& Entry,
                              double Alpha,
                              std::uint32_t Fallback)
{
    for (const WorkspaceMaterialRecord& Material : Scene.Materials)
    {
        if (Material.Reference != Entry.MaterialReference)
            continue;
        const ChannelSpecification& Albedo = Material.Material.Channel(ChannelSubject::AlbedoColour);
        if (!Albedo.ChannelDeclared || Albedo.Measured != ChannelMeasure::Reflectance ||
            Albedo.Source != ChannelSource::Constant || !Albedo.ConstantColour.ColourDeclared())
            break;

        const auto Byte = [](double Value) -> std::uint32_t
        {
            const double Clamped = std::max(0.0, std::min(1.0, Value));
            return static_cast<std::uint32_t>(Clamped * 255.0 + 0.5);
        };
        const std::uint32_t Packed = (Byte(Albedo.ConstantColour.RedCoordinate) << 16u)
                                   | (Byte(Albedo.ConstantColour.GreenCoordinate) << 8u)
                                   | Byte(Albedo.ConstantColour.BlueCoordinate);
        return Partial(Packed, Alpha);
    }
    return Partial(Fallback, Alpha);
}

void RecordCodexSceneProxy(RecordingSurface& Surface,
                           const PlaneExtent& Extent,
                           const ResolvedCamera& Camera,
                           const WorkspaceCodex& Scene,
                           bool SceneStanding,
                           const SceneDirectoryRows& Storage,
                           const SceneDirectoryContext& Applied,
                           double UnitScale)
{
    if (!SceneStanding)
        return;

    Surface.Confine(Extent);
    const ThemeToken FaceLit = Partial(0xFFFFFFu, 0.22);
    const ThemeToken Edge = Partial(0xE7E3D8u, 0.74);
    const ThemeToken SelectedEdge = Partial(0xFBBF24u, 0.95);
    const ThemeToken Floor = Partial(0xFFFFFFu, 0.08);

    for (std::uint32_t SceneIndex = 0u; SceneIndex < Scene.Scene.size(); ++SceneIndex)
    {
        const CodexSceneEntry& Entry = Scene.Scene[SceneIndex];
        if (Entry.Subject != CodexSceneSubject::Geometry)
            continue;

        const SpatialPoint Centre = CodexScenePosition(Entry, UnitScale);
        const ThemeToken Fill = CodexMaterialToken(Scene, Entry, 0.34, 0xF4F1E8u);
        const CodexSceneMesh* Mesh = nullptr;
        for (const CodexSceneMesh& Candidate : Scene.SceneMeshes)
            if (Candidate.Naming == Entry.GeometryReference)
            {
                Mesh = &Candidate;
                break;
            }
        if (Mesh != nullptr && Mesh->Positions.size() >= 9u && Mesh->Indices.size() >= 3u)
        {
            for (std::uint32_t Index = 0u; Index + 2u < Mesh->Indices.size(); Index += 3u)
            {
                float SX[3] = {};
                float SY[3] = {};
                bool TriangleStanding = true;
                for (std::uint32_t Corner = 0u; Corner < 3u; ++Corner)
                {
                    const std::uint32_t Vertex = Mesh->Indices[Index + Corner];
                    if (Vertex * 3u + 2u >= Mesh->Positions.size())
                    {
                        TriangleStanding = false;
                        break;
                    }
                    TriangleStanding = ProjectOffsetFromCamera(Camera, Extent, Centre,
                        Mesh->Positions[Vertex * 3u + 0u] * Entry.Scale[0] * UnitScale,
                        Mesh->Positions[Vertex * 3u + 1u] * Entry.Scale[1] * UnitScale,
                        Mesh->Positions[Vertex * 3u + 2u] * Entry.Scale[2] * UnitScale,
                        SX[Corner], SY[Corner]) && TriangleStanding;
                }
                if (TriangleStanding)
                {
                    const float Corners[6] = { SX[0], SY[0], SX[1], SY[1], SX[2], SY[2] };
                    Surface.Tongue(Corners, 3u, std::strstr(Entry.Naming.c_str(), "Floor") != nullptr ? Floor : Fill);
                }
            }
        }

        double HalfX = 0.0, HalfY = 0.0, HalfZ = 0.0;
        ResolveCodexProxyExtent(Entry, HalfX, HalfY, HalfZ);

        float X[8] = {};
        float Y[8] = {};
        const double Signs[8][3] =
        {
            { -1.0, -1.0, -1.0 }, {  1.0, -1.0, -1.0 }, {  1.0, -1.0,  1.0 }, { -1.0, -1.0,  1.0 },
            { -1.0,  1.0, -1.0 }, {  1.0,  1.0, -1.0 }, {  1.0,  1.0,  1.0 }, { -1.0,  1.0,  1.0 }
        };
        bool Standing = true;
        for (std::uint32_t Index = 0u; Index < 8u; ++Index)
            Standing = ProjectOffsetFromCamera(Camera, Extent, Centre,
                                              Signs[Index][0] * HalfX,
                                              Signs[Index][1] * HalfY,
                                              Signs[Index][2] * HalfZ,
                                              X[Index], Y[Index]) && Standing;
        if (!Standing)
            continue;

        const bool IsFloor = std::strstr(Entry.Naming.c_str(), "Floor") != nullptr;
        const bool Selected = [&]()
        {
            for (std::uint32_t Row = 0u; Row < Storage.RowCount && Row < SceneDirectoryContext::EntityLimit; ++Row)
                if (Storage.Rows[Row].Identity == 6200u + SceneIndex && Applied.EntitySelected[Row])
                    return true;
            return false;
        }();
        const auto Triangle = [&](std::uint32_t A, std::uint32_t B, std::uint32_t C, ThemeToken Colour)
        {
            const float Corners[6] = { X[A], Y[A], X[B], Y[B], X[C], Y[C] };
            Surface.Tongue(Corners, 3u, Colour);
        };
        const auto Line = [&](std::uint32_t A, std::uint32_t B)
        {
            const float PointsX[2] = { X[A], X[B] };
            const float PointsY[2] = { Y[A], Y[B] };
            Surface.Polyline(PointsX, PointsY, 2u, Selected ? SelectedEdge : Edge, Selected ? 1.8f : 1.1f);
        };

        if (IsFloor)
        {
            Triangle(0u, 1u, 2u, Floor);
            Triangle(0u, 2u, 3u, Floor);
        }
        else
        {
            Triangle(4u, 5u, 6u, FaceLit);
            Triangle(4u, 6u, 7u, FaceLit);
            Triangle(0u, 1u, 5u, Fill);
            Triangle(0u, 5u, 4u, Fill);
            Triangle(1u, 2u, 6u, Fill);
            Triangle(1u, 6u, 5u, Fill);
            Triangle(2u, 3u, 7u, Fill);
            Triangle(2u, 7u, 6u, Fill);
            Triangle(3u, 0u, 4u, Fill);
            Triangle(3u, 4u, 7u, Fill);
        }

        Line(0u, 1u); Line(1u, 2u); Line(2u, 3u); Line(3u, 0u);
        Line(4u, 5u); Line(5u, 6u); Line(6u, 7u); Line(7u, 4u);
        Line(0u, 4u); Line(1u, 5u); Line(2u, 6u); Line(3u, 7u);
    }
    Surface.Release();
}

void SeedSceneDirectoryTransformsFromCodex(const WorkspaceCodex& Scene,
                                           const SceneDirectoryRows& Storage,
                                           SceneDirectoryContext& Applied)
{
    if (Applied.TransformSeeded)
        return;
    for (std::uint32_t Row = 0u; Row < Storage.RowCount && Row < SceneDirectoryContext::EntityLimit; ++Row)
    {
        const StableRowIdentity Identity = Storage.Rows[Row].Identity;
        if (Identity < 6200u)
            continue;
        const std::uint32_t SceneIndex = static_cast<std::uint32_t>(Identity - 6200u);
        if (SceneIndex >= Scene.Scene.size())
            continue;
        const CodexSceneEntry& Entry = Scene.Scene[SceneIndex];
        for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
        {
            Applied.EntityPosition[Row][Axis] = Entry.Position[Axis];
            Applied.EntityRotation[Row][Axis] = Entry.Rotation[Axis];
            Applied.EntityScale[Row][Axis] = Entry.Scale[Axis];
        }
    }
    Applied.TransformSeeded = true;
}

void SynchroniseCodexTransformsFromSceneDirectory(WorkspaceCodex& Scene,
                                                  const SceneDirectoryRows& Storage,
                                                  const SceneDirectoryContext& Applied,
                                                  bool SceneStanding)
{
    if (!SceneStanding)
        return;
    for (std::uint32_t Row = 0u; Row < Storage.RowCount && Row < SceneDirectoryContext::EntityLimit; ++Row)
    {
        const StableRowIdentity Identity = Storage.Rows[Row].Identity;
        if (Identity < 6200u)
            continue;
        const std::uint32_t SceneIndex = static_cast<std::uint32_t>(Identity - 6200u);
        if (SceneIndex >= Scene.Scene.size())
            continue;
        CodexSceneEntry& Entry = Scene.Scene[SceneIndex];
        for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
        {
            Entry.Position[Axis] = Applied.EntityPosition[Row][Axis];
            Entry.Rotation[Axis] = Applied.EntityRotation[Row][Axis];
            Entry.Scale[Axis] = Applied.EntityScale[Row][Axis] == 0.0 ? 1.0 : Applied.EntityScale[Row][Axis];
        }
    }
}

}   // namespace Slate
