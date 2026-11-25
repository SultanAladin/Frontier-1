/*============================================================================================================================================
                                                  SIMULATIONSCENEDOCUMENT.CPP
============================================================================================================================================*/
// 🧩 Phase D25 Pure-TOML multi-domain simulation scene serializer, parser & SimulationCoordinatorExtension mounter

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationSceneDocument.h"
#include "Engine/Codecs/Simulation/ForceFieldCodec.h"
#include "Engine/SimulationSpace/EffectLayers/EffectLayerSpecification.h"

#include <fstream>
#include <sstream>

namespace Frontier::SimulationSpace
{

namespace
{

std::string FormatVec3(const std::array<float, 3>& v)
{
    using Codecs::Simulation::FormatCanonicalFloat;
    return "[" + FormatCanonicalFloat(v[0]) + ", " + FormatCanonicalFloat(v[1]) + ", " + FormatCanonicalFloat(v[2]) + "]";
}

std::array<float, 3> ReadVec3(
    const EffectLayers::TomlTable& tbl,
    std::string_view key,
    const std::array<float, 3>& fallback)
{
    const auto* val = tbl.Find(key);
    const auto* arr = (val != nullptr) ? val->AsArray() : nullptr;
    if (arr == nullptr || arr->Entries.size() < 3u || !arr->Entries[0] || !arr->Entries[1] || !arr->Entries[2])
    {
        return fallback;
    }
    return {
        static_cast<float>(arr->Entries[0]->AsDouble(fallback[0])),
        static_cast<float>(arr->Entries[1]->AsDouble(fallback[1])),
        static_cast<float>(arr->Entries[2]->AsDouble(fallback[2]))
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                SIMULATION SCENE CODEC
//------------------------------------------------------------------------------------------------------------------------

SimulationSceneDocument SimulationSceneCodec::BuildTracksideShowcaseDocument()
{
    SimulationSceneDocument doc {};
    doc.Title                  = "TracksideCoupledShowcase";
    doc.SchemaVersion          = 1u;
    doc.FixedStepHz            = 60.0f;
    doc.FluidGridCells         = { 16u, 16u, 16u };
    doc.FluidCellSizeMeters    = 0.22f;
    doc.FluidCentreMeters      = { -1.95f, 1.75f, -1.55f };
    doc.FluidBuoyancy          = 2.1f;
    doc.FluidCombustion        = 1.25f;
    doc.FluidPressureIters     = 12u;
    doc.EnableVehicleCoupling  = true;
    doc.ShippedVertexAtlasPath = "EngineContent/Simulation/Deformables/SportTireFem.VertexAtlas";
    doc.CameraWorldPosition    = { 4.10f, 2.35f, 4.95f };

    doc.Forces.push_back({
        "TrackCrosswind", ForceFields::ForceModelOrdinal::DirectionalWind,
        { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.15f, 0.25f }, 4.8f, 12.0f, ForceFields::ForceFalloffOrdinal::Constant
    });
    doc.Forces.push_back({
        "CampfireVortex", ForceFields::ForceModelOrdinal::RotationalColumn,
        { -1.95f, 1.0f, -1.55f }, { 0.0f, 1.0f, 0.0f }, 2.6f, 3.5f, ForceFields::ForceFalloffOrdinal::SmoothStep
    });

    doc.FluidSources.push_back({
        "TracksideCampfire", { -1.95f, 0.42f, -1.55f }, 0.42f, { 0.25f, 2.4f, 0.1f }, 1.65f, 2.85f, 1.20f
    });

    doc.ParticleClouds.push_back({
        "CampfireEmbers", { -1.95f, 0.48f, -1.55f }, 0.36f, 256u, 12.0f, 1.6f, 2.4f, 0.15f, 0.90f, 0.85f
    });

    doc.SoftBodies.push_back({
        "TrackBanner", "EngineContent/Simulation/Deformables/WovenCloth.SoftBody", { 0.30f, 1.78f, -2.05f }
    });
    doc.SoftBodies.push_back({
        "TrackFerns", "EngineContent/Simulation/Deformables/BotanicalFerns.SoftBody", { 2.35f, 1.78f, -1.35f }
    });

    doc.RigidColliders.push_back({
        42u, { 0.30f, 2.05f, -1.95f }, 0.42f, true
    });

    return doc;
}

std::string SimulationSceneCodec::EncodeToTomlText(const SimulationSceneDocument& doc)
{
    using Codecs::Simulation::FormatCanonicalFloat;

    std::ostringstream os;
    os << "# Frontier Unified Multi-Domain Simulation Scene (.VolumetricSpace)\n\n";
    os << "[Scene]\n";
    os << "Title = \"" << doc.Title << "\"\n";
    os << "SchemaVersion = " << doc.SchemaVersion << "\n";
    os << "FixedStepHz = " << FormatCanonicalFloat(doc.FixedStepHz) << "\n";
    os << "CameraWorldPosition = " << FormatVec3(doc.CameraWorldPosition) << "\n\n";

    os << "[Fluid]\n";
    os << "GridCells = [" << doc.FluidGridCells[0] << ", " << doc.FluidGridCells[1] << ", " << doc.FluidGridCells[2] << "]\n";
    os << "CellSizeMeters = " << FormatCanonicalFloat(doc.FluidCellSizeMeters) << "\n";
    os << "CentreMeters = " << FormatVec3(doc.FluidCentreMeters) << "\n";
    os << "Buoyancy = " << FormatCanonicalFloat(doc.FluidBuoyancy) << "\n";
    os << "Combustion = " << FormatCanonicalFloat(doc.FluidCombustion) << "\n";
    os << "PressureIterations = " << doc.FluidPressureIters << "\n\n";

    os << "[Vehicle]\n";
    os << "EnableCoupling = " << (doc.EnableVehicleCoupling ? "true" : "false") << "\n";
    os << "VertexAtlasPath = \"" << doc.ShippedVertexAtlasPath << "\"\n\n";

    for (const auto& f : doc.Forces)
    {
        os << "[[Force]]\n";
        os << "Identifier = \"" << f.Identifier << "\"\n";
        os << "ModelCode = " << static_cast<uint32_t>(f.ModelOrdinal) << "\n";
        os << "Position = " << FormatVec3(f.Position) << "\n";
        os << "Direction = " << FormatVec3(f.Direction) << "\n";
        os << "Strength = " << FormatCanonicalFloat(f.Strength) << "\n";
        os << "RadiusMeters = " << FormatCanonicalFloat(f.RadiusMeters) << "\n";
        os << "FalloffCode = " << static_cast<uint32_t>(f.Falloff) << "\n\n";
    }

    for (const auto& s : doc.FluidSources)
    {
        os << "[[FluidSource]]\n";
        os << "Identifier = \"" << s.Identifier << "\"\n";
        os << "Position = " << FormatVec3(s.Position) << "\n";
        os << "RadiusMeters = " << FormatCanonicalFloat(s.RadiusMeters) << "\n";
        os << "VelocityMps = " << FormatVec3(s.VelocityMps) << "\n";
        os << "Density = " << FormatCanonicalFloat(s.Density) << "\n";
        os << "Temperature = " << FormatCanonicalFloat(s.Temperature) << "\n";
        os << "Fuel = " << FormatCanonicalFloat(s.Fuel) << "\n\n";
    }

    for (const auto& c : doc.ParticleClouds)
    {
        os << "[[ParticleCloud]]\n";
        os << "Identifier = \"" << c.Identifier << "\"\n";
        os << "Position = " << FormatVec3(c.Position) << "\n";
        os << "RadiusMeters = " << FormatCanonicalFloat(c.RadiusMeters) << "\n";
        os << "ParticleCount = " << c.ParticleCount << "\n";
        os << "ReleaseRate = " << FormatCanonicalFloat(c.ReleaseRate) << "\n";
        os << "SpeedMps = " << FormatCanonicalFloat(c.SpeedMps) << "\n";
        os << "LifetimeSec = " << FormatCanonicalFloat(c.LifetimeSec) << "\n";
        os << "GravityY = " << FormatCanonicalFloat(c.GravityY) << "\n";
        os << "GasInfluence = " << FormatCanonicalFloat(c.GasInfluence) << "\n";
        os << "FieldInfluence = " << FormatCanonicalFloat(c.FieldInfluence) << "\n\n";
    }

    for (const auto& sb : doc.SoftBodies)
    {
        os << "[[SoftBody]]\n";
        os << "Label = \"" << sb.Label << "\"\n";
        os << "SoftBodyPath = \"" << sb.SoftBodyPath << "\"\n";
        os << "WorldOrigin = " << FormatVec3(sb.WorldOrigin) << "\n\n";
    }

    for (const auto& rc : doc.RigidColliders)
    {
        os << "[[RigidCollider]]\n";
        os << "BodySlotIndex = " << rc.BodySlotIndex << "\n";
        os << "CenterMeters = " << FormatVec3(rc.CenterMeters) << "\n";
        os << "RadiusMeters = " << FormatCanonicalFloat(rc.RadiusMeters) << "\n";
        os << "DynamicReply = " << (rc.DynamicReply ? "true" : "false") << "\n\n";
    }

    return os.str();
}

bool SimulationSceneCodec::DecodeFromTomlText(
    std::string_view tomlText,
    SimulationSceneDocument& outDoc,
    std::string* outError)
{
    std::string parseErr;
    const auto parsed = EffectLayers::ParseTomlSubset(tomlText, &parseErr);
    if (!parsed.has_value())
    {
        if (outError != nullptr) *outError = parseErr;
        return false;
    }

    const EffectLayers::TomlTable& root = *parsed;
    outDoc = {};

    if (const auto* sceneVal = root.Find("Scene"); sceneVal != nullptr && sceneVal->AsTable() != nullptr)
    {
        const auto& t = *sceneVal->AsTable();
        if (const auto* v = t.Find("Title")) outDoc.Title = std::string(v->AsString(outDoc.Title));
        if (const auto* v = t.Find("SchemaVersion")) outDoc.SchemaVersion = static_cast<uint32_t>(v->AsInt64(1));
        if (const auto* v = t.Find("FixedStepHz")) outDoc.FixedStepHz = static_cast<float>(v->AsDouble(60.0));
        outDoc.CameraWorldPosition = ReadVec3(t, "CameraWorldPosition", outDoc.CameraWorldPosition);
    }

    if (const auto* flVal = root.Find("Fluid"); flVal != nullptr && flVal->AsTable() != nullptr)
    {
        const auto& t = *flVal->AsTable();
        if (const auto* g = t.Find("GridCells"); g != nullptr && g->AsArray() != nullptr && g->AsArray()->Entries.size() >= 3u)
        {
            const auto& e = g->AsArray()->Entries;
            outDoc.FluidGridCells = {
                static_cast<uint32_t>(e[0] ? e[0]->AsInt64(16) : 16),
                static_cast<uint32_t>(e[1] ? e[1]->AsInt64(16) : 16),
                static_cast<uint32_t>(e[2] ? e[2]->AsInt64(16) : 16)
            };
        }
        if (const auto* v = t.Find("CellSizeMeters")) outDoc.FluidCellSizeMeters = static_cast<float>(v->AsDouble(0.22));
        outDoc.FluidCentreMeters = ReadVec3(t, "CentreMeters", outDoc.FluidCentreMeters);
        if (const auto* v = t.Find("Buoyancy")) outDoc.FluidBuoyancy = static_cast<float>(v->AsDouble(2.0));
        if (const auto* v = t.Find("Combustion")) outDoc.FluidCombustion = static_cast<float>(v->AsDouble(1.2));
        if (const auto* v = t.Find("PressureIterations")) outDoc.FluidPressureIters = static_cast<uint32_t>(v->AsInt64(12));
    }

    if (const auto* vhVal = root.Find("Vehicle"); vhVal != nullptr && vhVal->AsTable() != nullptr)
    {
        const auto& t = *vhVal->AsTable();
        if (const auto* v = t.Find("EnableCoupling")) outDoc.EnableVehicleCoupling = v->AsBool(true);
        if (const auto* v = t.Find("VertexAtlasPath")) outDoc.ShippedVertexAtlasPath = std::string(v->AsString(""));
    }

    if (const auto* arrVal = root.Find("Force"); arrVal != nullptr && arrVal->AsArray() != nullptr)
    {
        for (const auto& itemPtr : arrVal->AsArray()->Entries)
        {
            if (!itemPtr || itemPtr->AsTable() == nullptr) continue;
            const auto& t = *itemPtr->AsTable();
            SceneForceEntry e {};
            if (const auto* v = t.Find("Identifier")) e.Identifier = std::string(v->AsString("Force"));
            if (const auto* v = t.Find("ModelCode")) e.ModelOrdinal = static_cast<ForceFields::ForceModelOrdinal>(v->AsInt64(0));
            e.Position  = ReadVec3(t, "Position", e.Position);
            e.Direction = ReadVec3(t, "Direction", e.Direction);
            if (const auto* v = t.Find("Strength")) e.Strength = static_cast<float>(v->AsDouble(4.0));
            if (const auto* v = t.Find("RadiusMeters")) e.RadiusMeters = static_cast<float>(v->AsDouble(8.0));
            if (const auto* v = t.Find("FalloffCode")) e.Falloff = static_cast<ForceFields::ForceFalloffOrdinal>(v->AsInt64(0));
            outDoc.Forces.push_back(std::move(e));
        }
    }

    if (const auto* arrVal = root.Find("FluidSource"); arrVal != nullptr && arrVal->AsArray() != nullptr)
    {
        for (const auto& itemPtr : arrVal->AsArray()->Entries)
        {
            if (!itemPtr || itemPtr->AsTable() == nullptr) continue;
            const auto& t = *itemPtr->AsTable();
            SceneFluidSourceEntry e {};
            if (const auto* v = t.Find("Identifier")) e.Identifier = std::string(v->AsString("Source"));
            e.Position    = ReadVec3(t, "Position", e.Position);
            e.VelocityMps = ReadVec3(t, "VelocityMps", e.VelocityMps);
            if (const auto* v = t.Find("RadiusMeters")) e.RadiusMeters = static_cast<float>(v->AsDouble(0.4));
            if (const auto* v = t.Find("Density")) e.Density = static_cast<float>(v->AsDouble(1.5));
            if (const auto* v = t.Find("Temperature")) e.Temperature = static_cast<float>(v->AsDouble(2.5));
            if (const auto* v = t.Find("Fuel")) e.Fuel = static_cast<float>(v->AsDouble(1.0));
            outDoc.FluidSources.push_back(std::move(e));
        }
    }

    if (const auto* arrVal = root.Find("ParticleCloud"); arrVal != nullptr && arrVal->AsArray() != nullptr)
    {
        for (const auto& itemPtr : arrVal->AsArray()->Entries)
        {
            if (!itemPtr || itemPtr->AsTable() == nullptr) continue;
            const auto& t = *itemPtr->AsTable();
            SceneParticleCloudEntry e {};
            if (const auto* v = t.Find("Identifier")) e.Identifier = std::string(v->AsString("Particles"));
            e.Position = ReadVec3(t, "Position", e.Position);
            if (const auto* v = t.Find("RadiusMeters")) e.RadiusMeters = static_cast<float>(v->AsDouble(0.35));
            if (const auto* v = t.Find("ParticleCount")) e.ParticleCount = static_cast<uint32_t>(v->AsInt64(256));
            if (const auto* v = t.Find("ReleaseRate")) e.ReleaseRate = static_cast<float>(v->AsDouble(12.0));
            if (const auto* v = t.Find("SpeedMps")) e.SpeedMps = static_cast<float>(v->AsDouble(1.5));
            if (const auto* v = t.Find("LifetimeSec")) e.LifetimeSec = static_cast<float>(v->AsDouble(2.4));
            if (const auto* v = t.Find("GravityY")) e.GravityY = static_cast<float>(v->AsDouble(0.15));
            if (const auto* v = t.Find("GasInfluence")) e.GasInfluence = static_cast<float>(v->AsDouble(0.85));
            if (const auto* v = t.Find("FieldInfluence")) e.FieldInfluence = static_cast<float>(v->AsDouble(0.85));
            outDoc.ParticleClouds.push_back(std::move(e));
        }
    }

    if (const auto* arrVal = root.Find("SoftBody"); arrVal != nullptr && arrVal->AsArray() != nullptr)
    {
        for (const auto& itemPtr : arrVal->AsArray()->Entries)
        {
            if (!itemPtr || itemPtr->AsTable() == nullptr) continue;
            const auto& t = *itemPtr->AsTable();
            SceneSoftBodyPlacement e {};
            if (const auto* v = t.Find("Label")) e.Label = std::string(v->AsString("SoftBody"));
            if (const auto* v = t.Find("SoftBodyPath")) e.SoftBodyPath = std::string(v->AsString(""));
            e.WorldOrigin = ReadVec3(t, "WorldOrigin", e.WorldOrigin);
            outDoc.SoftBodies.push_back(std::move(e));
        }
    }

    if (const auto* arrVal = root.Find("RigidCollider"); arrVal != nullptr && arrVal->AsArray() != nullptr)
    {
        for (const auto& itemPtr : arrVal->AsArray()->Entries)
        {
            if (!itemPtr || itemPtr->AsTable() == nullptr) continue;
            const auto& t = *itemPtr->AsTable();
            SceneRigidColliderEntry e {};
            if (const auto* v = t.Find("BodySlotIndex")) e.BodySlotIndex = static_cast<uint32_t>(v->AsInt64(1));
            e.CenterMeters = ReadVec3(t, "CenterMeters", e.CenterMeters);
            if (const auto* v = t.Find("RadiusMeters")) e.RadiusMeters = static_cast<float>(v->AsDouble(0.4));
            if (const auto* v = t.Find("DynamicReply")) e.DynamicReply = v->AsBool(true);
            outDoc.RigidColliders.push_back(e);
        }
    }

    return true;
}

bool SimulationSceneCodec::EncodeToFile(
    const std::filesystem::path& targetPath,
    const SimulationSceneDocument& doc,
    std::string* outError)
{
    if (targetPath.has_parent_path())
    {
        std::error_code ec;
        std::filesystem::create_directories(targetPath.parent_path(), ec);
    }
    std::ofstream out(targetPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        if (outError != nullptr) *outError = "Failed to open target scene file for writing: " + targetPath.string();
        return false;
    }
    const std::string text = EncodeToTomlText(doc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

bool SimulationSceneCodec::DecodeFromFile(
    const std::filesystem::path& sourcePath,
    SimulationSceneDocument& outDoc,
    std::string* outError)
{
    std::ifstream in(sourcePath, std::ios::binary);
    if (!in.is_open())
    {
        if (outError != nullptr) *outError = "Failed to open scene file for reading: " + sourcePath.string();
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return DecodeFromTomlText(ss.str(), outDoc, outError);
}

SimulationCoordinatorParameters SimulationSceneCodec::BuildCoordinatorParameters(const SimulationSceneDocument& doc)
{
    SimulationCoordinatorParameters params {};
    params.FixedStepHz                        = doc.FixedStepHz;
    params.FluidGridCells                     = doc.FluidGridCells;
    params.FluidCellSizeMeters                = doc.FluidCellSizeMeters;
    params.FluidCentreMeters                  = doc.FluidCentreMeters;
    params.FluidSettings.Buoyancy             = doc.FluidBuoyancy;
    params.FluidSettings.Combustion           = doc.FluidCombustion;
    params.FluidSettings.PressureIterations   = doc.FluidPressureIters;
    params.DeformableParameters.EnableVehicleCoupling  = doc.EnableVehicleCoupling;
    params.DeformableParameters.ShippedVertexAtlasPath = doc.ShippedVertexAtlasPath;
    return params;
}

bool SimulationSceneCodec::MountIntoCoordinator(
    const SimulationSceneDocument& doc,
    SimulationCoordinatorExtension& coordinator,
    std::string* outError)
{
    if (!coordinator.QueryInitialized())
    {
        if (outError != nullptr) *outError = "SimulationCoordinatorExtension must be initialized before mounting a scene.";
        return false;
    }

    coordinator.AssignCameraWorldPosition(doc.CameraWorldPosition);
    coordinator.AccessForceFields().Clear();
    for (const auto& f : doc.Forces)
    {
        auto& entry = coordinator.AccessForceFields().AddForceNode(f.ModelOrdinal, f.Identifier);
        entry.Node.PositionMeters = f.Position;
        entry.Node.Direction      = f.Direction;
        entry.Node.Strength       = f.Strength;
        entry.Node.RadiusMeters   = f.RadiusMeters;
        entry.Node.Falloff        = f.Falloff;
    }

    coordinator.ClearFluidSources();
    for (const auto& s : doc.FluidSources)
    {
        Fluid::FluidReleaseSourceParameters src {};
        src.SourceId       = s.Identifier;
        src.Shape          = Fluid::ReleaseSourceShapeOrdinal::Sphere;
        src.PositionMeters = s.Position;
        src.RadiusMeters   = s.RadiusMeters;
        src.VelocityMps    = s.VelocityMps;
        src.Density        = s.Density;
        src.Temperature    = s.Temperature;
        src.Fuel           = s.Fuel;
        coordinator.AddFluidSource(src);
    }

    coordinator.ClearParticleClouds();
    for (const auto& c : doc.ParticleClouds)
    {
        ParticleSpace::ParticleCloudNode cloud {};
        cloud.NodeId                   = c.Identifier;
        cloud.Enabled                  = true;
        cloud.Evaluated.Shape          = ParticleSpace::ParticleShapeOrdinal::Sphere;
        cloud.Evaluated.Count          = c.ParticleCount;
        cloud.Evaluated.Rate           = c.ReleaseRate;
        cloud.Evaluated.Position       = c.Position;
        cloud.Evaluated.Radius         = c.RadiusMeters;
        cloud.Evaluated.Speed          = c.SpeedMps;
        cloud.Evaluated.Lifetime       = c.LifetimeSec;
        cloud.Evaluated.Gravity        = c.GravityY;
        cloud.Evaluated.GasInfluence   = c.GasInfluence;
        cloud.Evaluated.FieldInfluence = c.FieldInfluence;
        coordinator.AddParticleCloud(cloud);
    }

    coordinator.AccessDeformables().AccessWorldRegistry().ClearAll();
    for (const auto& sb : doc.SoftBodies)
    {
        std::string err;
        const uint32_t id = coordinator.AccessDeformables().AccessWorldRegistry().SpawnFromSoftBodyFile(
            sb.SoftBodyPath, sb.WorldOrigin, sb.Label, &err);
        if (id == 0u)
        {
            if (outError != nullptr) *outError = err;
            return false;
        }
    }

    std::vector<PhysicsSpace::RigidColliderProxy> proxies;
    proxies.reserve(doc.RigidColliders.size());
    for (const auto& rc : doc.RigidColliders)
    {
        PhysicsSpace::RigidColliderProxy p {};
        p.BodySlotIndex      = rc.BodySlotIndex;
        p.ShapeCode          = 0u;
        p.CenterMeters[0]    = rc.CenterMeters[0];
        p.CenterMeters[1]    = rc.CenterMeters[1];
        p.CenterMeters[2]    = rc.CenterMeters[2];
        p.RadiusMeters       = rc.RadiusMeters;
        p.DynamicResponseBit = rc.DynamicReply ? 1u : 0u;
        proxies.push_back(p);
    }
    coordinator.AssignRigidColliderProxies(proxies);
    return true;
}

std::vector<std::filesystem::path> SimulationSceneCodec::EnumerateShippedScenes(
    const std::filesystem::path& scenesDirectory)
{
    std::vector<std::filesystem::path> result;
    std::error_code ec;
    if (!std::filesystem::exists(scenesDirectory, ec) || !std::filesystem::is_directory(scenesDirectory, ec))
    {
        return result;
    }
    for (const auto& entry : std::filesystem::directory_iterator(scenesDirectory, ec))
    {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".VolumetricSpace")
        {
            result.push_back(entry.path());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace Frontier::SimulationSpace
