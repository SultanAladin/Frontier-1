/*==========================================================================================================================================
                                                  DYNAMICMATERIALREPOSITORY.CPP
==========================================================================================================================================*/
// 🧩 Implementation of Frontier's 13 modular asset-agnostic dynamic OpenPBR material recipes

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/Dynamic/DynamicMaterialRepository.h"

namespace Frontier::MaterialSpace::Dynamic
{

namespace
{

[[nodiscard]] SurfaceMaterial MakeObsidianFinish()
{
    SurfaceMaterial M {};
    M.MaterialName                  = "LiquidObsidianFinish";
    M.Diffuse.BaseColor             = { 0.012f, 0.015f, 0.024f };
    M.Diffuse.DiffuseRoughness      = 0.15f;
    M.Specular.Metalness            = 0.24f;
    M.Specular.Roughness            = 0.06f;
    M.Specular.DielectricIOR        = 1.54f;
    M.Clearcoat.CoatWeight          = 0.95f;
    M.Clearcoat.CoatRoughness       = 0.035f;
    M.Clearcoat.CoatIOR             = 1.56f;
    M.GlintFlakes.FlakeCoverage     = 0.28f;
    M.GlintFlakes.FlakePrimaryTint  = { 0.35f, 0.56f, 0.88f };
    M.GlintFlakes.FlakeHorizonTint  = { 0.18f, 0.42f, 0.78f };
    SanitizeSurfaceMaterial(M);
    return M;
}

[[nodiscard]] SurfaceMaterial MakeBrushedTitaniumFinish()
{
    SurfaceMaterial M {};
    M.MaterialName             = "BrushedAerospaceTitanium";
    M.Diffuse.BaseColor        = { 0.74f, 0.77f, 0.81f };
    M.Specular.Metalness       = 0.92f;
    M.Specular.Roughness       = 0.24f;
    M.Specular.Anisotropy      = 0.55f;
    M.Specular.EdgeTintF82     = { 0.92f, 0.94f, 0.98f };
    SanitizeSurfaceMaterial(M);
    return M;
}

void AppendStandardSliders(DynamicMaterialRecipe& Recipe)
{
    Recipe.ExposedSliders = {
        { "speed",    "Animation Speed",     0.0f,  4.0f, Recipe.Parameters.AnimationSpeed,       ParameterDriverMode::ConstantScalar },
        { "progress", "Transition Progress", 0.0f,  1.0f, Recipe.Parameters.TransitionProgress,   static_cast<ParameterDriverMode>(Recipe.Parameters.PrimaryDriverOrdinal) },
        { "scale",    "Feature Scale",       0.5f, 16.0f, Recipe.Parameters.DetailScale,          ParameterDriverMode::ConstantScalar },
        { "relief",   "Relief Strength",     0.0f,  2.0f, Recipe.Parameters.ReliefStrength,       ParameterDriverMode::ConstantScalar },
        { "seam",     "Seam Glow",           0.0f,  4.0f, Recipe.Parameters.SeamGlowIntensity,    ParameterDriverMode::ConstantScalar },
    };
}

} // namespace

DynamicMaterialRecipe ConstructDynamicMaterialPreset(DynamicPresetIdentity Identity)
{
    DynamicMaterialRecipe Recipe {};
    Recipe.PresetIdentity = Identity;
    Recipe.PrimaryFinish  = MakeObsidianFinish();

    switch (Identity)
    {
        case DynamicPresetIdentity::LatticeVoxelDissolve:
        {
            Recipe.RecipeId      = "lattice-voxel-dissolve";
            Recipe.DisplayTitle  = "Lattice Voxel Dissolve";
            Recipe.CategoryLabel = "Transitions";
            Recipe.SummaryNote   = "Modular cubic voxel lattice dissolve cutting across any OpenPBR finish with ionized cyan-white leading edge.";
            Recipe.Parameters.DetailScale         = 8.5f;
            Recipe.Parameters.AnimationSpeed      = 0.75f;
            Recipe.Parameters.TransitionProgress  = 0.46f;
            Recipe.Parameters.ReliefStrength      = 0.85f;
            Recipe.Parameters.SeamGlowIntensity   = 2.6f;
            Recipe.Parameters.PrimaryColorRgba    = { 0.012f, 0.016f, 0.025f, 1.0f };
            Recipe.Parameters.SecondaryColorRgba  = { 0.06f, 0.14f, 0.22f, 1.0f };
            Recipe.Parameters.TertiaryColorRgba   = { 0.14f, 0.85f, 1.00f, 1.0f };
            Recipe.Parameters.AccentRadianceRgba  = { 0.45f, 0.94f, 1.00f, 2.8f };
            Recipe.Parameters.PrimaryDriverOrdinal = static_cast<uint32_t>(ParameterDriverMode::ParticleLatticeLink);

            MaterialLayerEntry Finish {};
            Finish.LayerTitle     = "Primary Clearcoat Finish";
            Finish.Category       = MaterialLayerCategory::FinishLayer;
            Finish.FinishMaterial = Recipe.PrimaryFinish;
            Recipe.LayerStack.AppendLayer(Finish);

            MaterialLayerEntry Lattice {};
            Lattice.LayerTitle       = "Cubic Voxel Dissolve Mask";
            Lattice.Category         = MaterialLayerCategory::MaskModifierLayer;
            Lattice.FieldPattern     = ProceduralFieldPattern::CubicVoxelLattice;
            Lattice.MaskMode         = MaskModifierMode::VoxelClockCutoutMask;
            Lattice.DriverMode       = ParameterDriverMode::ParticleLatticeLink;
            Lattice.SeamGlowStrength = 2.6f;
            Lattice.ChannelMask      = CombineChannelBits(BlendChannelBitmask::ChannelOpacityCutout, BlendChannelBitmask::ChannelIncandescence);
            Recipe.LayerStack.AppendLayer(Lattice);
            break;
        }

        case DynamicPresetIdentity::VolcanicPahoehoeMagma:
        {
            Recipe.RecipeId      = "volcanic-pahoehoe-magma";
            Recipe.DisplayTitle  = "Volcanic Pahoehoe Magma";
            Recipe.CategoryLabel = "Nature";
            Recipe.SummaryNote   = "Photorealistic rough dark volcanic basalt slabs fractured by recessed convective blackbody magma fissures.";
            Recipe.PrimaryFinish.MaterialName             = "VolcanicBasaltSlab";
            Recipe.PrimaryFinish.Diffuse.BaseColor        = { 0.055f, 0.052f, 0.050f };
            Recipe.PrimaryFinish.Diffuse.DiffuseRoughness = 0.88f;
            Recipe.PrimaryFinish.Specular.Metalness       = 0.02f;
            Recipe.PrimaryFinish.Specular.Roughness       = 0.84f;
            Recipe.PrimaryFinish.Clearcoat.CoatWeight     = 0.0f;
            Recipe.PrimaryFinish.GlintFlakes.FlakeCoverage = 0.0f;

            Recipe.Parameters.DetailScale         = 3.4f;
            Recipe.Parameters.AnimationSpeed      = 0.45f;
            Recipe.Parameters.ReliefStrength      = 1.25f;
            Recipe.Parameters.SeamGlowIntensity   = 2.8f;
            Recipe.Parameters.PrimaryColorRgba    = { 0.052f, 0.049f, 0.048f, 1.0f };
            Recipe.Parameters.SecondaryColorRgba  = { 0.14f, 0.125f, 0.115f, 1.0f };
            Recipe.Parameters.TertiaryColorRgba   = { 1.00f, 0.24f, 0.02f, 1.0f };
            Recipe.Parameters.AccentRadianceRgba  = { 1.00f, 0.72f, 0.22f, 3.2f };
            Recipe.Parameters.PrimaryDriverOrdinal = static_cast<uint32_t>(ParameterDriverMode::PlaybackClock);

            MaterialLayerEntry Basalt {};
            Basalt.LayerTitle     = "Ropy Pahoehoe Basalt";
            Basalt.Category       = MaterialLayerCategory::FinishLayer;
            Basalt.FinishMaterial = Recipe.PrimaryFinish;
            Recipe.LayerStack.AppendLayer(Basalt);

            MaterialLayerEntry Faults {};
            Faults.LayerTitle   = "Dual-Scale Tectonic Fractures";
            Faults.Category     = MaterialLayerCategory::ProceduralFieldLayer;
            Faults.FieldPattern = ProceduralFieldPattern::DualScaleVoronoiFaults;
            Faults.ChannelMask  = CombineChannelBits(BlendChannelBitmask::ChannelReliefNormal, BlendChannelBitmask::ChannelRoughness);
            Recipe.LayerStack.AppendLayer(Faults);

            MaterialLayerEntry Magma {};
            Magma.LayerTitle       = "Recessed Blackbody Magma Veins";
            Magma.Category         = MaterialLayerCategory::MaskModifierLayer;
            Magma.MaskMode         = MaskModifierMode::RecessedCreviceMask;
            Magma.DriverMode       = ParameterDriverMode::PlaybackClock;
            Magma.SeamGlowStrength = 2.8f;
            Magma.ChannelMask      = static_cast<uint32_t>(BlendChannelBitmask::ChannelIncandescence);
            Recipe.LayerStack.AppendLayer(Magma);
            break;
        }

        case DynamicPresetIdentity::HexagonalEnergyBarrier:
        {
            Recipe.RecipeId           = "hexagonal-energy-barrier";
            Recipe.DisplayTitle       = "Hexagonal Energy Barrier";
            Recipe.CategoryLabel      = "Dynamic";
            Recipe.SummaryNote        = "Translucent sapphire Fresnel dome with razor-thin hexagonal grid and live geodesic shockwave ripples.";
            Recipe.TransparentSurface = true;
            Recipe.DoubleSidedSurface = true;
            Recipe.PrimaryFinish.MaterialName             = "EnergyBarrierDielectric";
            Recipe.PrimaryFinish.Diffuse.BaseColor        = { 0.02f, 0.10f, 0.22f };
            Recipe.PrimaryFinish.Specular.Metalness       = 0.05f;
            Recipe.PrimaryFinish.Specular.Roughness       = 0.08f;
            Recipe.PrimaryFinish.Clearcoat.CoatWeight     = 0.55f;
            Recipe.PrimaryFinish.GlintFlakes.FlakeCoverage = 0.0f;

            Recipe.Parameters.DetailScale         = 4.6f;
            Recipe.Parameters.AnimationSpeed      = 0.85f;
            Recipe.Parameters.OpacityScale        = 0.88f;
            Recipe.Parameters.SeamGlowIntensity   = 2.6f;
            Recipe.Parameters.PrimaryColorRgba    = { 0.13f, 0.72f, 1.00f, 1.0f };
            Recipe.Parameters.SecondaryColorRgba  = { 0.24f, 0.42f, 1.00f, 1.0f };
            Recipe.Parameters.TertiaryColorRgba   = { 0.55f, 0.96f, 1.00f, 1.0f };
            Recipe.Parameters.AccentRadianceRgba  = { 0.75f, 0.97f, 1.00f, 3.0f };
            Recipe.Parameters.PrimaryDriverOrdinal = static_cast<uint32_t>(ParameterDriverMode::WorldImpactPulse);

            MaterialLayerEntry Dome {};
            Dome.LayerTitle     = "Sapphire Fresnel Shell";
            Dome.Category       = MaterialLayerCategory::FinishLayer;
            Dome.FinishMaterial = Recipe.PrimaryFinish;
            Recipe.LayerStack.AppendLayer(Dome);

            MaterialLayerEntry Hex {};
            Hex.LayerTitle   = "Hexagonal Wireframe Lattice";
            Hex.Category     = MaterialLayerCategory::ProceduralFieldLayer;
            Hex.FieldPattern = ProceduralFieldPattern::HexagonalBarrierLattice;
            Hex.ChannelMask  = CombineChannelBits(BlendChannelBitmask::ChannelIncandescence, BlendChannelBitmask::ChannelOpacityCutout);
            Recipe.LayerStack.AppendLayer(Hex);

            MaterialLayerEntry Shock {};
            Shock.LayerTitle       = "Geodesic Impact Shockwaves";
            Shock.Category         = MaterialLayerCategory::MaskModifierLayer;
            Shock.MaskMode         = MaskModifierMode::GeodesicImpactRipple;
            Shock.DriverMode       = ParameterDriverMode::WorldImpactPulse;
            Shock.SeamGlowStrength = 2.9f;
            Shock.ChannelMask      = static_cast<uint32_t>(BlendChannelBitmask::ChannelIncandescence);
            Recipe.LayerStack.AppendLayer(Shock);
            break;
        }

        case DynamicPresetIdentity::SurfaceMaterialTransition:
        {
            Recipe.RecipeId        = "surface-material-transition";
            Recipe.DisplayTitle    = "Surface Material Transition";
            Recipe.CategoryLabel   = "Transitions";
            Recipe.SummaryNote     = "Modular Finish A <-> Finish B transition on any asset across an ionized hexagonal sweep front.";
            Recipe.PrimaryFinish   = MakeBrushedTitaniumFinish();
            Recipe.SecondaryFinish = MakeObsidianFinish();

            Recipe.Parameters.DetailScale         = 4.2f;
            Recipe.Parameters.AnimationSpeed      = 0.75f;
            Recipe.Parameters.TransitionProgress  = 0.50f;
            Recipe.Parameters.ReliefStrength      = 0.75f;
            Recipe.Parameters.SeamGlowIntensity   = 2.6f;
            Recipe.Parameters.PrimaryColorRgba    = { 0.74f, 0.78f, 0.83f, 1.0f };
            Recipe.Parameters.SecondaryColorRgba  = { 0.014f, 0.018f, 0.028f, 1.0f };
            Recipe.Parameters.TertiaryColorRgba   = { 0.16f, 0.85f, 1.00f, 1.0f };
            Recipe.Parameters.AccentRadianceRgba  = { 0.82f, 0.97f, 1.00f, 2.7f };
            Recipe.Parameters.PrimaryDriverOrdinal = static_cast<uint32_t>(ParameterDriverMode::PingPongOscillation);

            MaterialLayerEntry FinishA {};
            FinishA.LayerTitle     = "Finish A: Brushed Titanium";
            FinishA.Category       = MaterialLayerCategory::FinishLayer;
            FinishA.FinishMaterial = Recipe.PrimaryFinish;
            Recipe.LayerStack.AppendLayer(FinishA);

            MaterialLayerEntry Transition {};
            Transition.LayerTitle      = "Transition -> Liquid Obsidian";
            Transition.Category        = MaterialLayerCategory::SurfaceTransitionLayer;
            Transition.FieldPattern    = ProceduralFieldPattern::AnisotropicBrushedGrain;
            Transition.MaskMode        = MaskModifierMode::SweepWaveMask;
            Transition.DriverMode      = ParameterDriverMode::PingPongOscillation;
            Transition.FinishMaterial  = Recipe.PrimaryFinish;
            Transition.SecondaryFinish = Recipe.SecondaryFinish;
            Transition.SeamGlowStrength = 2.6f;
            Recipe.LayerStack.AppendLayer(Transition);
            break;
        }

        case DynamicPresetIdentity::HolographicTelemetryScan:
        {
            Recipe.RecipeId      = "holographic-telemetry-scan";
            Recipe.DisplayTitle  = "Holographic Telemetry Scan";
            Recipe.CategoryLabel = "Dynamic";
            Recipe.SummaryNote   = "Sweeping lidar laser scan plane with trailing orthographic contour wire grid and grazing holographic rim.";
            Recipe.PrimaryFinish = MakeObsidianFinish();

            Recipe.Parameters.DetailScale         = 5.2f;
            Recipe.Parameters.AnimationSpeed      = 0.85f;
            Recipe.Parameters.TransitionProgress  = 0.36f;
            Recipe.Parameters.SeamGlowIntensity   = 2.8f;
            Recipe.Parameters.PrimaryColorRgba    = { 0.015f, 0.025f, 0.042f, 1.0f };
            Recipe.Parameters.SecondaryColorRgba  = { 0.04f, 0.42f, 0.62f, 1.0f };
            Recipe.Parameters.TertiaryColorRgba   = { 0.22f, 0.88f, 1.00f, 1.0f };
            Recipe.Parameters.AccentRadianceRgba  = { 0.85f, 0.98f, 1.00f, 3.0f };
            Recipe.Parameters.PrimaryDriverOrdinal = static_cast<uint32_t>(ParameterDriverMode::PingPongOscillation);

            MaterialLayerEntry PrimaryFinish {};
            PrimaryFinish.LayerTitle     = "Dark Carbon-Obsidian Finish";
            PrimaryFinish.Category       = MaterialLayerCategory::FinishLayer;
            PrimaryFinish.FinishMaterial = Recipe.PrimaryFinish;
            Recipe.LayerStack.AppendLayer(PrimaryFinish);

            MaterialLayerEntry Scan {};
            Scan.LayerTitle       = "Lidar Scan Plane & Contour Wake";
            Scan.Category         = MaterialLayerCategory::MaskModifierLayer;
            Scan.FieldPattern     = ProceduralFieldPattern::OrthographicScanGrid;
            Scan.MaskMode         = MaskModifierMode::ElevationBandMask;
            Scan.DriverMode       = ParameterDriverMode::PingPongOscillation;
            Scan.SeamGlowStrength = 2.8f;
            Scan.ChannelMask      = static_cast<uint32_t>(BlendChannelBitmask::ChannelIncandescence);
            Recipe.LayerStack.AppendLayer(Scan);
            break;
        }

        case DynamicPresetIdentity::PhysicalOceanWater:
        {
            Recipe.RecipeId           = "physical-ocean-water";
            Recipe.DisplayTitle       = "Physical Ocean Water";
            Recipe.CategoryLabel      = "Nature";
            Recipe.SummaryNote        = "Animated multi-swell Gerstner waves, Beer-Lambert abyssal absorption, Schlick Fresnel & sunlit turquoise crests.";
            Recipe.TransparentSurface = true;
            Recipe.PrimaryFinish.MaterialName                     = "OceanWaterDielectric";
            Recipe.PrimaryFinish.Diffuse.BaseColor                = { 0.008f, 0.065f, 0.115f };
            Recipe.PrimaryFinish.Specular.Metalness               = 0.0f;
            Recipe.PrimaryFinish.Specular.Roughness               = 0.045f;
            Recipe.PrimaryFinish.Specular.DielectricIOR           = 1.333f;
            Recipe.PrimaryFinish.Clearcoat.CoatWeight             = 0.40f;
            Recipe.PrimaryFinish.Clearcoat.CoatRoughness          = 0.03f;
            Recipe.PrimaryFinish.Clearcoat.CoatIOR                = 1.333f;
            Recipe.PrimaryFinish.Transmission.TransmissionWeight  = 0.72f;
            Recipe.PrimaryFinish.Transmission.AttenuationColor    = { 0.06f, 0.42f, 0.56f };
            Recipe.PrimaryFinish.Subsurface.SubsurfaceWeight      = 0.48f;
            Recipe.PrimaryFinish.Subsurface.SubsurfaceColor       = { 0.04f, 0.54f, 0.62f };
            Recipe.PrimaryFinish.GlintFlakes.FlakeCoverage        = 0.0f;

            Recipe.Parameters.DetailScale         = 3.2f;
            Recipe.Parameters.AnimationSpeed      = 0.90f;
            Recipe.Parameters.ReliefStrength      = 1.05f;
            Recipe.Parameters.OpticalIor          = 1.333f;
            Recipe.Parameters.WaveAmplitude       = 0.75f;
            Recipe.Parameters.PrimaryColorRgba    = { 0.006f, 0.048f, 0.095f, 1.0f };
            Recipe.Parameters.SecondaryColorRgba  = { 0.03f, 0.36f, 0.48f, 1.0f };
            Recipe.Parameters.TertiaryColorRgba   = { 0.76f, 0.92f, 0.96f, 1.0f };
            Recipe.Parameters.PrimaryDriverOrdinal = static_cast<uint32_t>(ParameterDriverMode::PlaybackClock);

            MaterialLayerEntry Water {};
            Water.LayerTitle     = "Abyssal Dielectric Water (IOR 1.333)";
            Water.Category       = MaterialLayerCategory::FinishLayer;
            Water.FinishMaterial = Recipe.PrimaryFinish;
            Recipe.LayerStack.AppendLayer(Water);

            MaterialLayerEntry Waves {};
            Waves.LayerTitle   = "4-Swell Gerstner & Capillary Field";
            Waves.Category     = MaterialLayerCategory::ProceduralFieldLayer;
            Waves.FieldPattern = ProceduralFieldPattern::GerstnerOceanSwells;
            Waves.DriverMode   = ParameterDriverMode::PlaybackClock;
            Waves.ChannelMask  = CombineChannelBits(BlendChannelBitmask::ChannelReliefNormal, BlendChannelBitmask::ChannelAlbedo);
            Recipe.LayerStack.AppendLayer(Waves);
            break;
        }

        case DynamicPresetIdentity::LiquidObsidianClearcoat:
        {
            Recipe.RecipeId      = "liquid-obsidian-clearcoat";
            Recipe.DisplayTitle  = "Liquid Obsidian Clearcoat";
            Recipe.CategoryLabel = "Metals";
            Recipe.SummaryNote   = "Piano-obsidian deep clearcoat with Kemppinen microflakes and subtle azure horizon pearlescence.";
            Recipe.PrimaryFinish = MakeObsidianFinish();

            Recipe.Parameters.DetailScale         = 6.0f;
            Recipe.Parameters.AnimationSpeed      = 0.35f;
            Recipe.Parameters.PrimaryColorRgba    = { 0.010f, 0.013f, 0.022f, 1.0f };
            Recipe.Parameters.SecondaryColorRgba  = { 0.05f, 0.14f, 0.28f, 1.0f };
            Recipe.Parameters.TertiaryColorRgba   = { 0.30f, 0.56f, 0.92f, 1.0f };
            Recipe.Parameters.PrimaryDriverOrdinal = static_cast<uint32_t>(ParameterDriverMode::ConstantScalar);

            MaterialLayerEntry Finish {};
            Finish.LayerTitle     = "Obsidian Lacquer & Glint Microflakes";
            Finish.Category       = MaterialLayerCategory::FinishLayer;
            Finish.FieldPattern   = ProceduralFieldPattern::PearlescentMicroFlakes;
            Finish.FinishMaterial = Recipe.PrimaryFinish;
            Recipe.LayerStack.AppendLayer(Finish);
            break;
        }

        case DynamicPresetIdentity::RainStreakedSmokedGlass:
        {
            Recipe.RecipeId           = "rain-streaked-smoked-glass";
            Recipe.DisplayTitle       = "Rain-Streaked Smoked Glass";
            Recipe.CategoryLabel      = "Nature";
            Recipe.SummaryNote        = "Smoked architectural glass coated with gravity-driven refractive rain beads and glossy streak trails.";
            Recipe.TransparentSurface = true;
            Recipe.PrimaryFinish.MaterialName                    = "SmokedGlassFinish";
            Recipe.PrimaryFinish.Diffuse.BaseColor               = { 0.03f, 0.042f, 0.058f };
            Recipe.PrimaryFinish.Specular.Metalness              = 0.04f;
            Recipe.PrimaryFinish.Specular.Roughness              = 0.26f;
            Recipe.PrimaryFinish.Specular.DielectricIOR          = 1.52f;
            Recipe.PrimaryFinish.Transmission.TransmissionWeight = 0.45f;
            Recipe.PrimaryFinish.Transmission.AttenuationColor   = { 0.36f, 0.44f, 0.54f };
            Recipe.PrimaryFinish.GlintFlakes.FlakeCoverage       = 0.0f;

            Recipe.Parameters.DetailScale          = 4.5f;
            Recipe.Parameters.AnimationSpeed       = 0.95f;
            Recipe.Parameters.ReliefStrength       = 1.15f;
            Recipe.Parameters.WeatherRainIntensity = 0.80f;
            Recipe.Parameters.PrimaryColorRgba     = { 0.03f, 0.045f, 0.065f, 1.0f };
            Recipe.Parameters.SecondaryColorRgba   = { 0.18f, 0.26f, 0.34f, 1.0f };
            Recipe.Parameters.TertiaryColorRgba    = { 0.72f, 0.84f, 0.96f, 1.0f };
            Recipe.Parameters.PrimaryDriverOrdinal = static_cast<uint32_t>(ParameterDriverMode::WeatherClimate);

            MaterialLayerEntry Glass {};
            Glass.LayerTitle     = "Smoked Dielectric Glass";
            Glass.Category       = MaterialLayerCategory::FinishLayer;
            Glass.FinishMaterial = Recipe.PrimaryFinish;
            Recipe.LayerStack.AppendLayer(Glass);

            MaterialLayerEntry Rain {};
            Rain.LayerTitle   = "Gravity Rain Beads & Wet Tracks";
            Rain.Category     = MaterialLayerCategory::ProceduralFieldLayer;
            Rain.FieldPattern = ProceduralFieldPattern::RainBeadStreaks;
            Rain.DriverMode   = ParameterDriverMode::WeatherClimate;
            Rain.ChannelMask  = CombineChannelBits(BlendChannelBitmask::ChannelReliefNormal, BlendChannelBitmask::ChannelRoughness);
            Recipe.LayerStack.AppendLayer(Rain);
            break;
        }

        case DynamicPresetIdentity::WetAsphaltRainPuddles:
        {
            Recipe.RecipeId      = "wet-asphalt-rain-puddles";
            Recipe.DisplayTitle  = "Wet Asphalt & Rain Puddles";
            Recipe.CategoryLabel = "Nature";
            Recipe.SummaryNote   = "Porous crushed-aggregate asphalt flooding into mirror rain puddles with concentric raindrop ripples.";
            Recipe.PrimaryFinish.MaterialName                   = "CrushedAsphaltFinish";
            Recipe.PrimaryFinish.Diffuse.BaseColor              = { 0.14f, 0.145f, 0.155f };
            Recipe.PrimaryFinish.Diffuse.DiffuseRoughness       = 0.85f;
            Recipe.PrimaryFinish.Specular.Metalness             = 0.0f;
            Recipe.PrimaryFinish.Specular.Roughness             = 0.84f;
            Recipe.PrimaryFinish.PorosityLight.PorosityFraction = 0.65f;
            Recipe.PrimaryFinish.Clearcoat.CoatWeight           = 0.0f;
            Recipe.PrimaryFinish.GlintFlakes.FlakeCoverage      = 0.0f;

            Recipe.Parameters.DetailScale          = 4.0f;
            Recipe.Parameters.AnimationSpeed       = 0.85f;
            Recipe.Parameters.ReliefStrength       = 0.95f;
            Recipe.Parameters.WeatherRainIntensity = 0.68f;
            Recipe.Parameters.PrimaryColorRgba     = { 0.15f, 0.155f, 0.165f, 1.0f };
            Recipe.Parameters.SecondaryColorRgba   = { 0.03f, 0.035f, 0.042f, 1.0f };
            Recipe.Parameters.TertiaryColorRgba    = { 0.38f, 0.44f, 0.52f, 1.0f };
            Recipe.Parameters.PrimaryDriverOrdinal = static_cast<uint32_t>(ParameterDriverMode::WeatherClimate);

            MaterialLayerEntry Tarmac {};
            Tarmac.LayerTitle     = "Crushed Aggregate Asphalt";
            Tarmac.Category       = MaterialLayerCategory::FinishLayer;
            Tarmac.FinishMaterial = Recipe.PrimaryFinish;
            Recipe.LayerStack.AppendLayer(Tarmac);

            MaterialLayerEntry Puddles {};
            Puddles.LayerTitle   = "Climate Puddle Flood & Rain Ripples";
            Puddles.Category     = MaterialLayerCategory::MaskModifierLayer;
            Puddles.FieldPattern = ProceduralFieldPattern::CrushedAsphaltPuddles;
            Puddles.MaskMode     = MaskModifierMode::PuddleFloodMask;
            Puddles.DriverMode   = ParameterDriverMode::WeatherClimate;
            Puddles.ChannelMask  = CombineChannelBits(BlendChannelBitmask::ChannelRoughness, BlendChannelBitmask::ChannelReliefNormal);
            Recipe.LayerStack.AppendLayer(Puddles);
            break;
        }

        case DynamicPresetIdentity::HoarfrostSparkleSnow:
        {
            Recipe.RecipeId      = "hoarfrost-sparkle-snow";
            Recipe.DisplayTitle  = "Hoarfrost & Sparkle Snow";
            Recipe.CategoryLabel = "Nature";
            Recipe.SummaryNote   = "Wind-sculpted sastrugi snow drifts with cool subsurface scattering and diamond micro-crystal glints.";
            Recipe.PrimaryFinish.MaterialName                = "HoarfrostSnowFinish";
            Recipe.PrimaryFinish.Diffuse.BaseColor           = { 0.86f, 0.90f, 0.96f };
            Recipe.PrimaryFinish.Diffuse.DiffuseRoughness    = 0.55f;
            Recipe.PrimaryFinish.Specular.Metalness          = 0.0f;
            Recipe.PrimaryFinish.Specular.Roughness          = 0.42f;
            Recipe.PrimaryFinish.Subsurface.SubsurfaceWeight = 0.55f;
            Recipe.PrimaryFinish.Subsurface.SubsurfaceColor  = { 0.56f, 0.78f, 0.98f };
            Recipe.PrimaryFinish.GlintFlakes.FlakeCoverage   = 0.22f;
            Recipe.PrimaryFinish.GlintFlakes.FlakePrimaryTint = { 0.95f, 0.98f, 1.00f };
            Recipe.PrimaryFinish.Clearcoat.CoatWeight        = 0.0f;

            Recipe.Parameters.DetailScale          = 4.2f;
            Recipe.Parameters.AnimationSpeed       = 0.30f;
            Recipe.Parameters.ReliefStrength       = 0.75f;
            Recipe.Parameters.WeatherSnowDepth     = 0.90f;
            Recipe.Parameters.PrimaryColorRgba     = { 0.88f, 0.92f, 0.97f, 1.0f };
            Recipe.Parameters.SecondaryColorRgba   = { 0.62f, 0.76f, 0.94f, 1.0f };
            Recipe.Parameters.TertiaryColorRgba    = { 1.00f, 1.00f, 1.00f, 1.0f };
            Recipe.Parameters.PrimaryDriverOrdinal = static_cast<uint32_t>(ParameterDriverMode::WeatherClimate);

            MaterialLayerEntry Snow {};
            Snow.LayerTitle     = "Sastrugi Snow & Crystal Glints";
            Snow.Category       = MaterialLayerCategory::FinishLayer;
            Snow.FieldPattern   = ProceduralFieldPattern::SastrugiSparkleSnow;
            Snow.FinishMaterial = Recipe.PrimaryFinish;
            Recipe.LayerStack.AppendLayer(Snow);
            break;
        }

        case DynamicPresetIdentity::SubmergedLimestoneCaustics:
        {
            Recipe.RecipeId      = "submerged-limestone-caustics";
            Recipe.DisplayTitle  = "Submerged Limestone Caustics";
            Recipe.CategoryLabel = "Nature";
            Recipe.SummaryNote   = "Wet submerged riverbed stone illuminated by drifting chromatically dispersed refractive caustics.";
            Recipe.PrimaryFinish.MaterialName              = "SubmergedLimestoneFinish";
            Recipe.PrimaryFinish.Diffuse.BaseColor         = { 0.16f, 0.28f, 0.32f };
            Recipe.PrimaryFinish.Diffuse.DiffuseRoughness  = 0.62f;
            Recipe.PrimaryFinish.Specular.Metalness        = 0.0f;
            Recipe.PrimaryFinish.Specular.Roughness        = 0.38f;
            Recipe.PrimaryFinish.Clearcoat.CoatWeight      = 0.35f;
            Recipe.PrimaryFinish.Clearcoat.CoatRoughness   = 0.08f;
            Recipe.PrimaryFinish.GlintFlakes.FlakeCoverage = 0.0f;

            Recipe.Parameters.DetailScale         = 3.8f;
            Recipe.Parameters.AnimationSpeed      = 0.80f;
            Recipe.Parameters.ReliefStrength      = 0.70f;
            Recipe.Parameters.SeamGlowIntensity   = 1.6f;
            Recipe.Parameters.PrimaryColorRgba    = { 0.15f, 0.28f, 0.33f, 1.0f };
            Recipe.Parameters.SecondaryColorRgba  = { 0.07f, 0.18f, 0.24f, 1.0f };
            Recipe.Parameters.TertiaryColorRgba   = { 0.56f, 0.94f, 0.98f, 1.0f };
            Recipe.Parameters.AccentRadianceRgba  = { 0.50f, 0.92f, 0.98f, 1.5f };
            Recipe.Parameters.PrimaryDriverOrdinal = static_cast<uint32_t>(ParameterDriverMode::PlaybackClock);

            MaterialLayerEntry Stone {};
            Stone.LayerTitle     = "Wet Riverbed Limestone";
            Stone.Category       = MaterialLayerCategory::FinishLayer;
            Stone.FinishMaterial = Recipe.PrimaryFinish;
            Recipe.LayerStack.AppendLayer(Stone);

            MaterialLayerEntry Caustics {};
            Caustics.LayerTitle   = "Chromatic Refractive Caustic Field";
            Caustics.Category     = MaterialLayerCategory::ProceduralFieldLayer;
            Caustics.FieldPattern = ProceduralFieldPattern::RefractiveCausticField;
            Caustics.DriverMode   = ParameterDriverMode::PlaybackClock;
            Caustics.ChannelMask  = CombineChannelBits(BlendChannelBitmask::ChannelAlbedo, BlendChannelBitmask::ChannelIncandescence);
            Recipe.LayerStack.AppendLayer(Caustics);
            break;
        }

        case DynamicPresetIdentity::LiquidChromeMercury:
        {
            Recipe.RecipeId      = "liquid-chrome-mercury";
            Recipe.DisplayTitle  = "Liquid Chrome Mercury";
            Recipe.CategoryLabel = "Metals";
            Recipe.SummaryNote   = "Mirror liquid conductor sculpted by intersecting harmonic capillary waves.";
            Recipe.PrimaryFinish.MaterialName              = "LiquidMercuryConductor";
            Recipe.PrimaryFinish.Diffuse.BaseColor         = { 0.86f, 0.89f, 0.94f };
            Recipe.PrimaryFinish.Specular.Metalness        = 0.98f;
            Recipe.PrimaryFinish.Specular.Roughness        = 0.055f;
            Recipe.PrimaryFinish.Specular.EdgeTintF82      = { 0.96f, 0.98f, 1.00f };
            Recipe.PrimaryFinish.Clearcoat.CoatWeight      = 0.0f;
            Recipe.PrimaryFinish.GlintFlakes.FlakeCoverage = 0.0f;

            Recipe.Parameters.DetailScale         = 3.4f;
            Recipe.Parameters.AnimationSpeed      = 0.95f;
            Recipe.Parameters.ReliefStrength      = 0.85f;
            Recipe.Parameters.WaveAmplitude       = 0.65f;
            Recipe.Parameters.PrimaryColorRgba    = { 0.86f, 0.89f, 0.94f, 1.0f };
            Recipe.Parameters.SecondaryColorRgba  = { 0.56f, 0.62f, 0.72f, 1.0f };
            Recipe.Parameters.TertiaryColorRgba   = { 1.00f, 1.00f, 1.00f, 1.0f };
            Recipe.Parameters.PrimaryDriverOrdinal = static_cast<uint32_t>(ParameterDriverMode::PlaybackClock);

            MaterialLayerEntry Chrome {};
            Chrome.LayerTitle     = "Mirror Liquid Conductor";
            Chrome.Category       = MaterialLayerCategory::FinishLayer;
            Chrome.FinishMaterial = Recipe.PrimaryFinish;
            Recipe.LayerStack.AppendLayer(Chrome);

            MaterialLayerEntry Ripples {};
            Ripples.LayerTitle   = "Harmonic Capillary Waves";
            Ripples.Category     = MaterialLayerCategory::ProceduralFieldLayer;
            Ripples.FieldPattern = ProceduralFieldPattern::CapillaryMercuryWaves;
            Ripples.DriverMode   = ParameterDriverMode::PlaybackClock;
            Ripples.ChannelMask  = static_cast<uint32_t>(BlendChannelBitmask::ChannelReliefNormal);
            Recipe.LayerStack.AppendLayer(Ripples);
            break;
        }

        case DynamicPresetIdentity::AnodizedThinFilmIridescence:
        {
            Recipe.RecipeId      = "anodized-thin-film-iridescence";
            Recipe.DisplayTitle  = "Anodized Thin-Film Iridescence";
            Recipe.CategoryLabel = "Metals";
            Recipe.SummaryNote   = "Brushed gunmetal titanium coated with Belcour-Barla optical wave-interference thin film.";
            Recipe.PrimaryFinish.MaterialName                         = "AnodizedThinFilmAlloy";
            Recipe.PrimaryFinish.Diffuse.BaseColor                    = { 0.16f, 0.17f, 0.21f };
            Recipe.PrimaryFinish.Specular.Metalness                   = 0.92f;
            Recipe.PrimaryFinish.Specular.Roughness                   = 0.18f;
            Recipe.PrimaryFinish.ThinFilm.ThinFilmWeight              = 1.0f;
            Recipe.PrimaryFinish.ThinFilm.ThinFilmThicknessNanometers = 470.0f;
            Recipe.PrimaryFinish.ThinFilm.ThinFilmIOR                 = 2.15f;
            Recipe.PrimaryFinish.Clearcoat.CoatWeight                 = 0.25f;
            Recipe.PrimaryFinish.GlintFlakes.FlakeCoverage            = 0.0f;

            Recipe.Parameters.DetailScale         = 2.8f;
            Recipe.Parameters.AnimationSpeed      = 0.45f;
            Recipe.Parameters.ThinFilmNanometers  = 470.0f;
            Recipe.Parameters.ThinFilmSpread      = 190.0f;
            Recipe.Parameters.PrimaryColorRgba    = { 0.16f, 0.17f, 0.21f, 1.0f };
            Recipe.Parameters.SecondaryColorRgba  = { 0.48f, 0.22f, 0.78f, 1.0f };
            Recipe.Parameters.TertiaryColorRgba   = { 0.18f, 0.90f, 0.65f, 1.0f };
            Recipe.Parameters.PrimaryDriverOrdinal = static_cast<uint32_t>(ParameterDriverMode::PlaybackClock);

            MaterialLayerEntry Alloy {};
            Alloy.LayerTitle     = "Brushed Gunmetal Titanium";
            Alloy.Category       = MaterialLayerCategory::FinishLayer;
            Alloy.FinishMaterial = Recipe.PrimaryFinish;
            Recipe.LayerStack.AppendLayer(Alloy);

            MaterialLayerEntry Film {};
            Film.LayerTitle   = "Belcour-Barla Optical Thin-Film Drift";
            Film.Category     = MaterialLayerCategory::ProceduralFieldLayer;
            Film.FieldPattern = ProceduralFieldPattern::ThinFilmThicknessDrift;
            Film.DriverMode   = ParameterDriverMode::PlaybackClock;
            Film.ChannelMask  = static_cast<uint32_t>(BlendChannelBitmask::ChannelThinFilm);
            Recipe.LayerStack.AppendLayer(Film);
            break;
        }
    }

    AppendStandardSliders(Recipe);
    SynchronizeRecipeParameters(Recipe);
    return Recipe;
}

std::vector<DynamicMaterialRecipe> BuildAllDynamicMaterialPresets()
{
    std::vector<DynamicMaterialRecipe> Presets;
    Presets.reserve(DynamicPresetTotalCount);
    for (uint32_t Index = 0u; Index < DynamicPresetTotalCount; ++Index)
    {
        Presets.push_back(ConstructDynamicMaterialPreset(static_cast<DynamicPresetIdentity>(Index)));
    }
    return Presets;
}

bool FindDynamicMaterialPreset(
    std::string_view       RecipeId,
    DynamicMaterialRecipe& OutRecipe)
{
    for (uint32_t Index = 0u; Index < DynamicPresetTotalCount; ++Index)
    {
        DynamicMaterialRecipe Candidate = ConstructDynamicMaterialPreset(static_cast<DynamicPresetIdentity>(Index));
        if (Candidate.RecipeId == RecipeId)
        {
            OutRecipe = std::move(Candidate);
            return true;
        }
    }
    return false;
}

} // namespace Frontier::MaterialSpace::Dynamic
