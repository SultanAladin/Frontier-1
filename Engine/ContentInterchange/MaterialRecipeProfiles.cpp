/*==========================================================================================================================================
                                                    MATERIALRECIPEPROFILES.CPP
==========================================================================================================================================*/
// 🧩 Native port of the Alloy recipe profiles (materialProfiles.js): the designer-visible control set
// for every shipped material family. Labels, groups, ranges and the stratum/pool addresses each
// gesture writes. Values only - the baker and the shader remain the single source of truth, so
// every slider here projects into the same coefficients the HTML controls write.

#include "Engine/ContentInterchange/MaterialRecipeProfiles.h"

namespace Frontier
{
namespace
{

// 📝 One entry per shipped recipeId; mirrors the JS recipes object verbatim. Fixed entries seed the
// values the family pins regardless of the saved material; Controls carry group, range and targets.
const std::vector<MaterialRecipeProfile> Profiles{
    {
        "paint", 0, "Automotive paint",
        "A layered finish. Paint, flakes and clearcoat are tuned separately.",
        false, false,
        {{"sheen", 0.f}, {"anisotropy", 0.f}, {"ior", 1.5f}, {"coatIor", 1.5f}},
        {{"color", "Paint color"}},
        {
            {"roughness", "Paint roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.12f, 0.42f}}},
            {"paintDepth", "Paint depth", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"depth", 0.15f, 1.8f}}},
            {"coatGloss", "Clearcoat gloss", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"coatRoughness", 0.19f, 0.035f}, {"coat", 0.65f, 1.f}}},
            {"flakes", "Flake coverage", "flakes", true, "flakes", 0.f, 1.f, 0.01f, "", false, false, "", {}},
            {"flakeSize", "Flake size", "flakes", true, "flakeSize", 0.001f, 1000.f, 0.01f, "µm", true, true, "", {}},
            {"flakeSparkle", "Flake sparkle", "flakes", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"flakeRoughnessMin", 0.32f, 0.055f}, {"flakeRoughnessMax", 0.5f, 0.18f}, {"flakeTilt", 0.08f, 0.4f}}},
            {"flakeReflectivity", "Flake reflectivity", "flakes", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"flakeMetalnessMin", 0.72f, 0.97f}, {"flakeMetalnessMax", 0.88f, 1.f}}},
            {"flakeScale", "Flake repetition", "detail", true, "flakeScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"flakeLayers", "Flake layers", "flakes", true, "flakeLayers", 1.f, 4.f, 1.f, "", false, false, "", {}},
            {"flakeDepth", "Buried flake tint", "flakes", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"flakeLayerDepth", 0.1f, 0.85f}}},
            {"paintTexture", "Orange-peel texture", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"orangePeel", 0.f, 0.9f}}},
            {"orangePeelScale", "Texture scale", "detail", true, "orangePeelScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "bareMetal", 1, "Uncoated metal / alloy",
        "A conductor with approximate RGB reflectance. Alloy grades are representative finishes, not measured spectral data.",
        false, true,
        {{"metalness", 1.f}, {"coat", 0.f}, {"sheen", 0.f}, {"anisotropy", 0.f}},
        {{"color", "Conductor reflectance"}},
        {
            {"polish", "Metal polish", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.7f, 0.045f}}},
            {"grain", "Surface tooth", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.f, 1.f}}},
        },
        {
            {"metalScratch_density", "Scratch density", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDensity", 0.f, 12.f}}},
            {"metalScratch_length", "Scratch length", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchLength", 0.1f, 1.65f}}},
            {"metalScratch_width", "Scratch width", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchWidth", 0.002f, 0.035f}}},
            {"metalScratch_depth", "Scratch depth", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDepth", 0.f, 0.006f}}},
            {"metalScratch_spread", "Direction spread", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchSpread", 0.f, 1.f}}},
            {"metalScratch_bend", "Scratch curvature", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchBend", 0.f, 0.7f}}},
            {"scratchAngle", "Scratch direction", "scratches", true, "scratchAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
            {"metalScratch_scratchScale", "Scratch field scale", "scratches", true, "scratchScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"metalScratch_surfaceSeed", "Pattern seed", "scratches", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
    },
    {
        "brushedMetal", 1, "Brushed alloy",
        "Directional microbrushing and a restrained metallic response.",
        false, true,
        {{"metalness", 1.f}, {"coat", 0.15f}, {"sheen", 0.f}},
        {{"color", "Alloy tint"}},
        {
            {"roughness", "Brushed Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.22f, 0.62f}}},
            {"directionality", "Directional sheen", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"anisotropy", 0.3f, 0.95f}}},
            {"detailScale", "Brush spacing", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
            {"metalScratch_density", "Scratch density", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDensity", 0.f, 12.f}}},
            {"metalScratch_length", "Scratch length", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchLength", 0.1f, 1.65f}}},
            {"metalScratch_width", "Scratch width", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchWidth", 0.002f, 0.035f}}},
            {"metalScratch_depth", "Scratch depth", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDepth", 0.f, 0.006f}}},
            {"metalScratch_spread", "Direction spread", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchSpread", 0.f, 1.f}}},
            {"metalScratch_bend", "Scratch curvature", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchBend", 0.f, 0.7f}}},
            {"scratchAngle", "Scratch direction", "scratches", true, "scratchAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
            {"metalScratch_scratchScale", "Scratch field scale", "scratches", true, "scratchScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"metalScratch_surfaceSeed", "Pattern seed", "scratches", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
    },
    {
        "polishedMetal", 1, "Polished metal",
        "A conductor with a controlled polish, rather than arbitrary dielectric settings.",
        false, true,
        {{"metalness", 1.f}, {"sheen", 0.f}, {"anisotropy", 0.05f}},
        {{"color", "Metal tint"}},
        {
            {"polish", "Metal polish", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.36f, 0.045f}}},
            {"coatGloss", "Protective finish", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"coat", 0.f, 0.45f}, {"coatRoughness", 0.18f, 0.05f}}},
            {"detailScale", "Machining scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
            {"metalScratch_density", "Scratch density", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDensity", 0.f, 12.f}}},
            {"metalScratch_length", "Scratch length", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchLength", 0.1f, 1.65f}}},
            {"metalScratch_width", "Scratch width", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchWidth", 0.002f, 0.035f}}},
            {"metalScratch_depth", "Scratch depth", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDepth", 0.f, 0.006f}}},
            {"metalScratch_spread", "Direction spread", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchSpread", 0.f, 1.f}}},
            {"metalScratch_bend", "Scratch curvature", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchBend", 0.f, 0.7f}}},
            {"scratchAngle", "Scratch direction", "scratches", true, "scratchAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
            {"metalScratch_scratchScale", "Scratch field scale", "scratches", true, "scratchScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"metalScratch_surfaceSeed", "Pattern seed", "scratches", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
    },
    {
        "ceramic", 2, "Carbon ceramic",
        "A porous, sintered surface with broad, subdued reflections.",
        false, false,
        {{"metalness", 0.25f}, {"coat", 0.08f}, {"sheen", 0.f}},
        {{"color", "Ceramic color"}},
        {
            {"roughness", "Ceramic Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.45f, 0.94f}}},
            {"porosity", "Porosity", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"depth", 0.1f, 1.4f}}},
            {"detailScale", "Grain scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "rubber", 3, "Rubber",
        "A non-metallic elastomer with a bounded matte finish and micrograin.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"sheen", 0.f}, {"ior", 1.5f}},
        {{"color", "Rubber color"}},
        {
            {"roughness", "Rubber Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.6f, 0.98f}}},
            {"grain", "Micrograin", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"depth", 0.1f, 0.8f}}},
            {"detailScale", "Grain scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "cotton", 4, "Cotton",
        "Matte, soft yarns. Warp and weft colors remain independent.",
        true, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"ior", 1.5f}},
        {{"warpColor", "Warp yarn · lengthwise"}, {"weftColor", "Weft yarn · crosswise"}},
        {
            {"roughness", "Cotton Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.72f, 0.97f}, {"sheenRoughness", 0.55f, 0.9f}}},
            {"softness", "Yarn softness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"fuzz", 0.15f, 0.85f}, {"fuzzLength", 0.12f, 0.7f}, {"sheen", 0.2f, 0.55f}}},
            {"detailScale", "Thread scale", "construction", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"definition", "Weave definition", "construction", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"weaveRelief", 0.08f, 1.f}, {"fiberDetail", 8.f, 28.f, true}}},
            {"weaveAngle", "Thread direction", "construction", true, "weaveAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
        },
        {
        },
    },
    {
        "denim", 4, "Denim",
        "Warp-faced dyed cotton. Lighter weft yarns show through the twill.",
        true, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"ior", 1.5f}},
        {{"warpColor", "Warp yarn · lengthwise"}, {"weftColor", "Weft yarn · crosswise"}},
        {
            {"roughness", "Denim Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.68f, 0.95f}, {"sheenRoughness", 0.45f, 0.85f}}},
            {"softness", "Yarn softness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"fuzz", 0.15f, 0.7f}, {"fuzzLength", 0.12f, 0.65f}, {"sheen", 0.25f, 0.6f}}},
            {"denimFade", "Indigo fade", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"denimFade", 0.f, 0.9f}}},
            {"slub", "Slub character", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"slub", 0.f, 1.f}}},
            {"detailScale", "Thread scale", "construction", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"definition", "Weave definition", "construction", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"weaveRelief", 0.08f, 1.f}, {"fiberDetail", 8.f, 28.f, true}}},
            {"weaveAngle", "Thread direction", "construction", true, "weaveAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
        },
        {
        },
    },
    {
        "linen", 4, "Linen",
        "Dry, irregular yarns with restrained sheen and a crisp weave.",
        true, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"ior", 1.5f}},
        {{"warpColor", "Warp yarn · lengthwise"}, {"weftColor", "Weft yarn · crosswise"}},
        {
            {"roughness", "Linen Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.78f, 0.98f}, {"sheenRoughness", 0.6f, 0.95f}}},
            {"softness", "Yarn softness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"fuzz", 0.05f, 0.45f}, {"fuzzLength", 0.08f, 0.4f}, {"sheen", 0.12f, 0.4f}}},
            {"detailScale", "Thread scale", "construction", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"definition", "Weave definition", "construction", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"weaveRelief", 0.08f, 1.f}, {"fiberDetail", 8.f, 28.f, true}}},
            {"weaveAngle", "Thread direction", "construction", true, "weaveAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
        },
        {
        },
    },
    {
        "silk", 4, "Silk / satin",
        "Long floating yarns, directional lustre and a fine, smooth finish.",
        true, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"ior", 1.55f}},
        {{"warpColor", "Warp yarn · lengthwise"}, {"weftColor", "Weft yarn · crosswise"}},
        {
            {"roughness", "Silk Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.12f, 0.38f}, {"sheenRoughness", 0.18f, 0.5f}}},
            {"lustre", "Silk lustre", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"anisotropy", 0.45f, 0.92f}, {"specular", 0.65f, 1.f}, {"sheen", 0.4f, 0.9f}}},
            {"detailScale", "Thread scale", "construction", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"definition", "Weave definition", "construction", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"weaveRelief", 0.08f, 1.f}, {"fiberDetail", 8.f, 28.f, true}}},
            {"weaveAngle", "Thread direction", "construction", true, "weaveAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
        },
        {
        },
    },
    {
        "suede", 4, "Suede / microfiber",
        "A brushed nap, with softly constrained reflection and fiber length.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"anisotropy", 0.f}, {"ior", 1.5f}},
        {{"color", "Suede dye"}},
        {
            {"roughness", "Suede Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.82f, 0.98f}, {"sheenRoughness", 0.45f, 0.9f}}},
            {"softness", "Nap softness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"fuzz", 0.35f, 1.f}, {"sheen", 0.55f, 1.f}}},
            {"pile", "Nap length", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"fuzzLength", 0.1f, 0.9f}}},
            {"detailScale", "Nap scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "velvet", 4, "Velvet",
        "Soft pile, diffuse body color and a grazing sheen. No metal or coat controls.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"anisotropy", 0.f}, {"ior", 1.5f}},
        {{"color", "Velvet dye"}},
        {
            {"roughness", "Velvet Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.78f, 0.97f}, {"sheenRoughness", 0.3f, 0.85f}}},
            {"softness", "Pile softness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"fuzz", 0.3f, 1.f}, {"sheen", 0.55f, 1.f}}},
            {"pile", "Pile length", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"fuzzLength", 0.3f, 1.5f}}},
        },
        {
        },
    },
    {
        "wool", 4, "Wool",
        "Full-bodied yarns, a soft surface and naturally broad reflections.",
        true, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"ior", 1.5f}},
        {{"warpColor", "Warp yarn · lengthwise"}, {"weftColor", "Weft yarn · crosswise"}},
        {
            {"roughness", "Wool Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.75f, 0.98f}, {"sheenRoughness", 0.5f, 0.9f}}},
            {"softness", "Wool softness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"fuzz", 0.35f, 1.f}, {"fuzzLength", 0.25f, 1.4f}, {"sheen", 0.25f, 0.7f}}},
            {"detailScale", "Thread scale", "construction", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"definition", "Weave definition", "construction", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"weaveRelief", 0.08f, 1.f}, {"fiberDetail", 8.f, 28.f, true}}},
            {"weaveAngle", "Thread direction", "construction", true, "weaveAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
        },
        {
        },
    },
    {
        "woven", 4, "Woven textile",
        "Yarn structure and color, without unrelated PBR parameters.",
        true, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"ior", 1.5f}},
        {{"warpColor", "Warp yarn · lengthwise"}, {"weftColor", "Weft yarn · crosswise"}},
        {
            {"roughness", "Textile Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.65f, 0.96f}, {"sheenRoughness", 0.45f, 0.9f}}},
            {"softness", "Yarn softness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"fuzz", 0.1f, 0.8f}, {"fuzzLength", 0.1f, 0.9f}, {"sheen", 0.2f, 0.65f}}},
            {"detailScale", "Thread scale", "construction", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"definition", "Weave definition", "construction", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"weaveRelief", 0.08f, 1.f}, {"fiberDetail", 8.f, 28.f, true}}},
            {"weaveAngle", "Thread direction", "construction", true, "weaveAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
        },
        {
        },
    },
    {
        "glass", 5, "Optical glass",
        "Tint, clarity and refraction within an automotive-glass range.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"sheen", 0.f}},
        {{"color", "Glass tint"}},
        {
            {"clarity", "Glass clarity", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.2f, 0.005f}}},
            {"thickness", "Glass thickness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"depth", 0.05f, 3.f}}},
            {"refraction", "Edge refraction", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"ior", 1.45f, 1.62f}}},
        },
        {
        },
    },
    {
        "brake", 6, "Machined brake iron",
        "Concentric tool marks with a bounded metallic finish.",
        false, true,
        {{"metalness", 0.96f}, {"coat", 0.06f}, {"sheen", 0.f}, {"anisotropy", 0.f}},
        {{"color", "Iron tint"}},
        {
            {"roughness", "Disc Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.28f, 0.7f}}},
            {"relief", "Machining depth", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"depth", 0.05f, 0.75f}}},
            {"detailScale", "Machining scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
            {"metalScratch_density", "Scratch density", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDensity", 0.f, 12.f}}},
            {"metalScratch_length", "Scratch length", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchLength", 0.1f, 1.65f}}},
            {"metalScratch_width", "Scratch width", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchWidth", 0.002f, 0.035f}}},
            {"metalScratch_depth", "Scratch depth", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDepth", 0.f, 0.006f}}},
            {"metalScratch_spread", "Direction spread", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchSpread", 0.f, 1.f}}},
            {"metalScratch_bend", "Scratch curvature", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchBend", 0.f, 0.7f}}},
            {"scratchAngle", "Scratch direction", "scratches", true, "scratchAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
            {"metalScratch_scratchScale", "Scratch field scale", "scratches", true, "scratchScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"metalScratch_surfaceSeed", "Pattern seed", "scratches", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
    },
    {
        "carbon", 7, "Carbon composite",
        "Reflective bundles beneath a resin finish. The weave stays carbon-like.",
        false, false,
        {{"metalness", 0.8f}, {"coat", 0.9f}, {"ior", 1.5f}, {"coatIor", 1.5f}},
        {{"color", "Composite tint"}},
        {
            {"roughness", "Carbon finish", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.2f, 0.5f}, {"coatRoughness", 0.045f, 0.19f}}},
            {"relief", "Bundle definition", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"weaveRelief", 0.1f, 0.8f}, {"fiberDetail", 8.f, 24.f, true}}},
            {"directionality", "Directional contrast", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"anisotropy", 0.4f, 0.92f}}},
            {"detailScale", "Bundle scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"weaveAngle", "Weave direction", "detail", true, "weaveAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
        },
        {
        },
    },
    {
        "glossPlastic", 8, "Gloss polymer",
        "A glossy dielectric finish. Metalness and optical constants are handled for you.",
        false, false,
        {{"metalness", 0.f}, {"sheen", 0.f}, {"ior", 1.55f}, {"coatIor", 1.5f}},
        {{"color", "Polymer color"}},
        {
            {"gloss", "Plastic gloss", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.32f, 0.065f}, {"coat", 0.35f, 1.f}, {"coatRoughness", 0.18f, 0.04f}}},
            {"grain", "Surface texture", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.f, 0.25f}}},
            {"detailScale", "Texture scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
            {"metalScratch_density", "Scratch density", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDensity", 0.f, 12.f}}},
            {"metalScratch_length", "Scratch length", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchLength", 0.1f, 1.65f}}},
            {"metalScratch_width", "Scratch width", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchWidth", 0.002f, 0.035f}}},
            {"metalScratch_depth", "Scratch depth", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDepth", 0.f, 0.006f}}},
            {"metalScratch_spread", "Direction spread", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchSpread", 0.f, 1.f}}},
            {"metalScratch_bend", "Scratch curvature", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchBend", 0.f, 0.7f}}},
            {"scratchAngle", "Scratch direction", "scratches", true, "scratchAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
            {"metalScratch_scratchScale", "Scratch field scale", "scratches", true, "scratchScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"metalScratch_surfaceSeed", "Pattern seed", "scratches", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
    },
    {
        "engineering", 9, "Engineering polymer",
        "A representative molded grade, not a certification of chemical, mechanical or optical properties.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"sheen", 0.f}, {"ior", 1.5f}},
        {{"color", "Polymer tint"}},
        {
            {"roughness", "Polymer finish", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.05f, 0.88f}}},
            {"grain", "Mold texture", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.f, 1.f}}},
            {"detailScale", "Tool texture scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
            {"metalScratch_density", "Scratch density", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDensity", 0.f, 12.f}}},
            {"metalScratch_length", "Scratch length", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchLength", 0.1f, 1.65f}}},
            {"metalScratch_width", "Scratch width", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchWidth", 0.002f, 0.035f}}},
            {"metalScratch_depth", "Scratch depth", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDepth", 0.f, 0.006f}}},
            {"metalScratch_spread", "Direction spread", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchSpread", 0.f, 1.f}}},
            {"metalScratch_bend", "Scratch curvature", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchBend", 0.f, 0.7f}}},
            {"scratchAngle", "Scratch direction", "scratches", true, "scratchAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
            {"metalScratch_scratchScale", "Scratch field scale", "scratches", true, "scratchScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"metalScratch_surfaceSeed", "Pattern seed", "scratches", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
    },
    {
        "grainPlastic", 9, "Grained polymer",
        "Molded grain with softer, non-metallic highlights.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.08f}, {"sheen", 0.f}, {"ior", 1.55f}},
        {{"color", "Polymer color"}},
        {
            {"roughness", "Polymer Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.42f, 0.82f}}},
            {"grain", "Molded grain", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.15f, 1.2f}}},
            {"detailScale", "Grain scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
            {"metalScratch_density", "Scratch density", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDensity", 0.f, 12.f}}},
            {"metalScratch_length", "Scratch length", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchLength", 0.1f, 1.65f}}},
            {"metalScratch_width", "Scratch width", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchWidth", 0.002f, 0.035f}}},
            {"metalScratch_depth", "Scratch depth", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDepth", 0.f, 0.006f}}},
            {"metalScratch_spread", "Direction spread", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchSpread", 0.f, 1.f}}},
            {"metalScratch_bend", "Scratch curvature", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchBend", 0.f, 0.7f}}},
            {"scratchAngle", "Scratch direction", "scratches", true, "scratchAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
            {"metalScratch_scratchScale", "Scratch field scale", "scratches", true, "scratchScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"metalScratch_surfaceSeed", "Pattern seed", "scratches", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
    },
    {
        "wornPlastic", 10, "Worn polymer",
        "Raised grain is polished down; the valleys keep their texture.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"sheen", 0.f}, {"ior", 1.55f}},
        {{"color", "Polymer color"}},
        {
            {"roughness", "Polymer Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.42f, 0.82f}}},
            {"wear", "Polymer wear", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"wear", 0.f, 1.f}}},
            {"wearBlend", "Wear softness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"wearSoftness", 0.25f, 1.f}}},
            {"wornGloss", "Worn polish", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"wornRoughness", 0.4f, 0.1f}}},
            {"grain", "Molded grain", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.15f, 1.f}}},
            {"detailScale", "Grain scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
            {"metalScratch_density", "Scratch density", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDensity", 0.f, 12.f}}},
            {"metalScratch_length", "Scratch length", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchLength", 0.1f, 1.65f}}},
            {"metalScratch_width", "Scratch width", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchWidth", 0.002f, 0.035f}}},
            {"metalScratch_depth", "Scratch depth", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDepth", 0.f, 0.006f}}},
            {"metalScratch_spread", "Direction spread", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchSpread", 0.f, 1.f}}},
            {"metalScratch_bend", "Scratch curvature", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchBend", 0.f, 0.7f}}},
            {"scratchAngle", "Scratch direction", "scratches", true, "scratchAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
            {"metalScratch_scratchScale", "Scratch field scale", "scratches", true, "scratchScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"metalScratch_surfaceSeed", "Pattern seed", "scratches", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
    },
    {
        "leather", 11, "Automotive leather",
        "Vector-authored furrows and pores with continuous repeat variation. Relief drives the grain and its finish.",
        false, false,
        {{"metalness", 0.f}, {"ior", 1.48f}, {"coatIor", 1.5f}},
        {{"color", "Leather dye"}},
        {
            {"roughness", "Leather Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.3f, 0.72f}, {"coatRoughness", 0.18f, 0.4f}}},
            {"grain", "Grain character", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.15f, 1.f}}},
            {"patina", "Patina", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"wear", 0.f, 0.8f}, {"wearSoftness", 0.45f, 0.9f}, {"wornRoughness", 0.34f, 0.15f}}},
            {"leatherSheen", "Finish sheen", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"coat", 0.05f, 0.45f}, {"sheen", 0.1f, 0.35f}}},
            {"hideVariation", "Repeat variation", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"hideVariation", 0.f, 1.f}}},
            {"detailScale", "Grain scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "clay", 12, "Clay",
        "Aggregate and wetness. A procedural surface approximation, not a volume simulation.",
        false, false,
        {{"metalness", 0.f}, {"sheen", 0.f}, {"ior", 1.5f}},
        {{"color", "Clay body"}},
        {
            {"roughness", "Clay Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.3f, 0.98f}}},
            {"wetness", "Clay wetness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"moisture", 0.f, 1.f}, {"coat", 0.f, 0.45f}, {"coatRoughness", 0.25f, 0.08f}}},
            {"grain", "Aggregate", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.05f, 1.2f}}},
            {"detailScale", "Aggregate scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "wax", 13, "Wax",
        "Marbling and soft transmission. Real-time wrap scattering, not volumetric subsurface transport.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"sheen", 0.f}, {"ior", 1.44f}},
        {{"color", "Wax dye"}},
        {
            {"roughness", "Wax Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.15f, 0.7f}}},
            {"scattering", "Wax softness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scattering", 0.1f, 1.f}, {"translucency", 0.03f, 0.3f}}},
            {"depth", "Body thickness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"depth", 0.1f, 2.f}}},
            {"grain", "Wax marbling", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.f, 0.8f}}},
            {"detailScale", "Marble scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "skin", 14, "Skin",
        "Pigment, pores and gentle wrap scattering. An illustrative skin shader, not measured multilayer SSS.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"sheen", 0.f}, {"ior", 1.4f}},
        {{"color", "Skin pigment"}, {"secondaryColor", "Undertone"}},
        {
            {"roughness", "Skin Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.32f, 0.78f}}},
            {"scattering", "Soft tissue response", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scattering", 0.f, 0.85f}}},
            {"freckles", "Pigment variation", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"freckles", 0.f, 1.f}}},
            {"grain", "Pore definition", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.05f, 1.2f}}},
            {"detailScale", "Pore scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "paper", 15, "Paper / card",
        "Pulp fibers, surface tooth and optional structural ribs. No printed image or texture input.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"sheen", 0.1f}, {"ior", 1.47f}},
        {{"color", "Paper stock"}, {"secondaryColor", "Fiber tint"}},
        {
            {"roughness", "Paper Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.25f, 0.98f}}},
            {"grain", "Paper tooth", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.05f, 1.2f}}},
            {"fiberContrast", "Visible fibers", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"fiberContrast", 0.f, 0.7f}}},
            {"detailScale", "Fiber scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "rust", 16, "Oxidized iron",
        "Metal underneath; porous dielectric oxide above. Coverage changes color, roughness, metalness and height.",
        false, true,
        {{"metalness", 1.f}, {"coat", 0.f}, {"sheen", 0.f}, {"anisotropy", 0.f}},
        {{"color", "Iron reflectance"}, {"secondaryColor", "Oxide color"}},
        {
            {"roughness", "Iron Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.22f, 0.65f}}},
            {"oxidation", "Rust coverage", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"oxidation", 0.f, 1.f}}},
            {"grain", "Oxide crust", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.1f, 1.2f}}},
            {"detailScale", "Oxide scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
            {"metalScratch_density", "Scratch density", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDensity", 0.f, 12.f}}},
            {"metalScratch_length", "Scratch length", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchLength", 0.1f, 1.65f}}},
            {"metalScratch_width", "Scratch width", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchWidth", 0.002f, 0.035f}}},
            {"metalScratch_depth", "Scratch depth", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDepth", 0.f, 0.006f}}},
            {"metalScratch_spread", "Direction spread", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchSpread", 0.f, 1.f}}},
            {"metalScratch_bend", "Scratch curvature", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchBend", 0.f, 0.7f}}},
            {"scratchAngle", "Scratch direction", "scratches", true, "scratchAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
            {"metalScratch_scratchScale", "Scratch field scale", "scratches", true, "scratchScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"metalScratch_surfaceSeed", "Pattern seed", "scratches", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
    },
    {
        "solar", 17, "Photovoltaic module",
        "Procedural cells and conductive grid beneath glass. A surface model, not an electrical simulation.",
        false, false,
        {{"metalness", 0.55f}, {"coat", 0.95f}, {"sheen", 0.f}, {"ior", 1.5f}, {"coatIor", 1.5f}},
        {{"color", "Silicon color"}, {"secondaryColor", "Contact metal"}},
        {
            {"roughness", "Cell Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.12f, 0.5f}}},
            {"gloss", "Cover glass gloss", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"coatRoughness", 0.15f, 0.025f}}},
            {"busbar", "Busbar width", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"busbarWidth", 0.006f, 0.04f}}},
            {"detailScale", "Cell scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "golf", 18, "Dimpled golf-ball cover",
        "Staggered recessed dimples in the normal field. Surface detail only; no displacement of the silhouette.",
        false, false,
        {{"metalness", 0.f}, {"sheen", 0.f}, {"ior", 1.5f}},
        {{"color", "Cover color"}},
        {
            {"roughness", "Cover Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.18f, 0.6f}}},
            {"dimple", "Dimple depth", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"dimpleDepth", 0.002f, 0.025f}}},
            {"gloss", "Protective gloss", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"coat", 0.15f, 0.9f}, {"coatRoughness", 0.18f, 0.045f}}},
            {"detailScale", "Dimple scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "jersey", 19, "Jersey knit",
        "Interlocking V-shaped yarn loops, not an over/under weave. Scale follows the cloth UVs.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"ior", 1.5f}},
        {{"color", "Yarn dye"}, {"secondaryColor", "Melange fiber"}},
        {
            {"roughness", "Jersey Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.7f, 0.97f}}},
            {"softness", "Knit softness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"fuzz", 0.1f, 0.8f}, {"sheen", 0.2f, 0.7f}}},
            {"grain", "Loop relief", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.1f, 1.2f}}},
            {"detailScale", "Stitch scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"weaveAngle", "Stitch direction", "detail", true, "weaveAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
        },
        {
        },
    },
    {
        "led", 20, "LED emitter array",
        "Recessed packages, domed lenses, emitter dies, contacts and PCB traces. No bloom or light cast on neighboring objects.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.25f}, {"sheen", 0.f}, {"ior", 1.5f}},
        {{"color", "Substrate color"}, {"emissionColor", "Emitted light"}},
        {
            {"brightness", "Emitter brightness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"emissionStrength", 0.f, 8.f}}},
            {"pixel", "Emitter fill", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"pixelFill", 0.2f, 0.94f}}},
            {"roughness", "Package Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.2f, 0.65f}}},
            {"lens", "Lens dome", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"lensDome", 0.1f, 1.f}}},
            {"housing", "Housing relief", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"packageDepth", 0.1f, 1.f}}},
            {"detailScale", "Pixel scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "corrugated", 21, "Corrugated conductor",
        "Metal optics reused under an axial rib field. Relief changes normals, not the smooth pipe silhouette.",
        false, true,
        {{"metalness", 1.f}, {"coat", 0.f}, {"sheen", 0.f}, {"anisotropy", 0.f}},
        {{"color", "Conductor reflectance"}},
        {
            {"polish", "Metal polish", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.6f, 0.08f}}},
            {"rib", "Rib relief", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"ribDepth", 0.001f, 0.04f}}},
            {"detailScale", "Rib frequency", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
            {"metalScratch_density", "Scratch density", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDensity", 0.f, 12.f}}},
            {"metalScratch_length", "Scratch length", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchLength", 0.1f, 1.65f}}},
            {"metalScratch_width", "Scratch width", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchWidth", 0.002f, 0.035f}}},
            {"metalScratch_depth", "Scratch depth", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDepth", 0.f, 0.006f}}},
            {"metalScratch_spread", "Direction spread", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchSpread", 0.f, 1.f}}},
            {"metalScratch_bend", "Scratch curvature", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchBend", 0.f, 0.7f}}},
            {"scratchAngle", "Scratch direction", "scratches", true, "scratchAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
            {"metalScratch_scratchScale", "Scratch field scale", "scratches", true, "scratchScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"metalScratch_surfaceSeed", "Pattern seed", "scratches", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
    },
    {
        "corrugatedSteel", 21, "Galvanised garage steel",
        "Zinc-coated IBR-style garage / cladding sheet. Choose round, box-rib or roller-door corrugation. No measured BRDF or structural certification is implied.",
        false, true,
        {{"metalness", 1.f}, {"coat", 0.f}, {"sheen", 0.f}, {"anisotropy", 0.f}},
        {{"color", "Zinc reflectance"}},
        {
            {"polish", "Sheet roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.72f, 0.08f}}},
            {"ribDepth", "Rib depth", "construction", true, "ribDepth", 0.f, 0.4f, 0.01f, "scene units", false, false, "", {}},
            {"detailScale", "Rib frequency", "construction", true, "detailScale", 0.1f, 20.f, 0.01f, "×", true, false, "", {}},
            {"weaveAngle", "Rib direction", "construction", true, "weaveAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
            {"sheetOxidation", "Weathering", "detail", true, "sheetOxidation", 0.f, 1.f, 0.01f, "", false, false, "", {}},
            {"sheetSpangle", "Spangle character", "detail", true, "sheetSpangle", 0.f, 1.f, 0.01f, "", false, false, "", {}},
        },
        {
            {"metalScratch_density", "Scratch density", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDensity", 0.f, 12.f}}},
            {"metalScratch_length", "Scratch length", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchLength", 0.1f, 1.65f}}},
            {"metalScratch_width", "Scratch width", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchWidth", 0.002f, 0.035f}}},
            {"metalScratch_depth", "Scratch depth", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDepth", 0.f, 0.006f}}},
            {"metalScratch_spread", "Direction spread", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchSpread", 0.f, 1.f}}},
            {"metalScratch_bend", "Scratch curvature", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchBend", 0.f, 0.7f}}},
            {"scratchAngle", "Scratch direction", "scratches", true, "scratchAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
            {"metalScratch_scratchScale", "Scratch field scale", "scratches", true, "scratchScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"metalScratch_surfaceSeed", "Pattern seed", "scratches", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
    },
    {
        "corrugatedZinc", 21, "Corrugated zinc sheet",
        "Zinc conductor with rounded ribs, spangle and optional white-oxide weathering. Normal relief only; the mesh silhouette is unchanged.",
        false, true,
        {{"metalness", 1.f}, {"coat", 0.f}, {"sheen", 0.f}, {"anisotropy", 0.f}},
        {{"color", "Zinc reflectance"}},
        {
            {"polish", "Sheet roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.72f, 0.08f}}},
            {"ribDepth", "Rib depth", "construction", true, "ribDepth", 0.f, 0.4f, 0.01f, "scene units", false, false, "", {}},
            {"detailScale", "Rib frequency", "construction", true, "detailScale", 0.1f, 20.f, 0.01f, "×", true, false, "", {}},
            {"weaveAngle", "Rib direction", "construction", true, "weaveAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
            {"sheetOxidation", "Weathering", "detail", true, "sheetOxidation", 0.f, 1.f, 0.01f, "", false, false, "", {}},
            {"sheetSpangle", "Spangle character", "detail", true, "sheetSpangle", 0.f, 1.f, 0.01f, "", false, false, "", {}},
        },
        {
            {"metalScratch_density", "Scratch density", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDensity", 0.f, 12.f}}},
            {"metalScratch_length", "Scratch length", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchLength", 0.1f, 1.65f}}},
            {"metalScratch_width", "Scratch width", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchWidth", 0.002f, 0.035f}}},
            {"metalScratch_depth", "Scratch depth", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDepth", 0.f, 0.006f}}},
            {"metalScratch_spread", "Direction spread", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchSpread", 0.f, 1.f}}},
            {"metalScratch_bend", "Scratch curvature", "scratches", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchBend", 0.f, 0.7f}}},
            {"scratchAngle", "Scratch direction", "scratches", true, "scratchAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
            {"metalScratch_scratchScale", "Scratch field scale", "scratches", true, "scratchScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"metalScratch_surfaceSeed", "Pattern seed", "scratches", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
    },
    {
        "tiles", 22, "Glazed ceramic tiles",
        "Glaze on the raised tile face; matte grout in the recess. No image maps.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.85f}, {"sheen", 0.f}, {"ior", 1.5f}},
        {{"color", "Glaze color"}, {"secondaryColor", "Grout color"}},
        {
            {"roughness", "Glaze Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.08f, 0.55f}}},
            {"grout", "Grout width", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"groutWidth", 0.01f, 0.16f}}},
            {"variation", "Tile variation", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"tileVariation", 0.f, 0.5f}}},
            {"relief", "Edge relief", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.1f, 1.2f}}},
            {"detailScale", "Tile repetition", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "granite", 23, "Granite",
        "Irregular quartz, feldspar and mica grains. A representative stone, not a scanned slab.",
        false, false,
        {{"metalness", 0.f}, {"sheen", 0.f}, {"ior", 1.52f}},
        {{"color", "Feldspar"}, {"secondaryColor", "Quartz"}, {"tertiaryColor", "Mica"}},
        {
            {"roughness", "Stone Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.12f, 0.8f}}},
            {"gloss", "Stone polish", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"coat", 0.f, 0.5f}, {"coatRoughness", 0.22f, 0.055f}}},
            {"grain", "Crystal relief", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.05f, 1.f}}},
            {"detailScale", "Mineral scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "marble", 24, "Marble",
        "Warped multiscale mineral veins in a polished stone matrix.",
        false, false,
        {{"metalness", 0.f}, {"sheen", 0.f}, {"ior", 1.5f}},
        {{"color", "Stone body"}, {"secondaryColor", "Mineral veins"}},
        {
            {"roughness", "Stone Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.08f, 0.7f}}},
            {"veins", "Vein width", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"veinWidth", 0.006f, 0.12f}}},
            {"gloss", "Stone polish", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"coat", 0.f, 0.6f}, {"coatRoughness", 0.2f, 0.045f}}},
            {"detailScale", "Vein scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "concrete", 25, "Concrete",
        "Cement, aggregate and sunken air voids. Board-form presets retain their construction marks.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"sheen", 0.f}, {"ior", 1.5f}},
        {{"color", "Cement color"}, {"secondaryColor", "Pore color"}},
        {
            {"roughness", "Concrete Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.65f, 0.98f}}},
            {"pores", "Air voids", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"poreDensity", 0.f, 1.f}}},
            {"grain", "Aggregate relief", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.1f, 1.5f}}},
            {"detailScale", "Aggregate scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "stucco", 26, "Lime / mineral stucco",
        "Dry plaster with raised, multiscale grain. Surface relief is generated, not sampled.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"sheen", 0.f}, {"ior", 1.5f}},
        {{"color", "Plaster pigment"}},
        {
            {"roughness", "Stucco Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.75f, 0.99f}}},
            {"grain", "Plaster relief", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.1f, 1.5f}}},
            {"detailScale", "Grain scale", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
        },
        {
        },
    },
    {
        "scratches", 29, "Scratches · surface study",
        "Finite, tapered cuts with seeded lengths, widths, direction and depth. Tiny lips catch the light. The same field is available as an optional layer on metals.",
        false, false,
        {{"metalness", 1.f}, {"coat", 0.f}, {"sheen", 0.f}, {"anisotropy", 0.f}},
        {{"color", "Test metal"}},
        {
            {"roughness", "Test surface Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.12f, 0.65f}}},
            {"density", "Scratch density", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDensity", 0.f, 12.f}}},
            {"length", "Scratch length", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchLength", 0.1f, 1.65f}}},
            {"width", "Scratch width", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchWidth", 0.002f, 0.035f}}},
            {"depth", "Scratch depth", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchDepth", 0.f, 0.006f}}},
            {"spread", "Direction spread", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchSpread", 0.f, 1.f}}},
            {"bend", "Scratch curvature", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"scratchBend", 0.f, 0.7f}}},
            {"weaveAngle", "Preferred direction", "detail", true, "weaveAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
            {"scratchScale", "Scratch field scale", "detail", true, "scratchScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"surfaceSeed", "Pattern seed", "detail", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
        {
        },
    },
    {
        "crocodile", 30, "Crocodile-style leather",
        "One continuous hide: softly depressed shared creases, small scale pores, and a grain-responsive finish. No inset tile borders.",
        false, false,
        {{"metalness", 0.f}, {"sheen", 0.12f}, {"ior", 1.48f}},
        {{"color", "Scale dye"}, {"secondaryColor", "Scale variation"}, {"tertiaryColor", "Crease dye"}},
        {
            {"roughness", "Leather Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.25f, 0.8f}}},
            {"hideVariation", "Repeat variation", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"hideVariation", 0.f, 1.f}}},
            {"relief", "Scale relief", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.1f, 1.3f}}},
            {"joints", "Crease width", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"groutWidth", 0.008f, 0.065f}}},
            {"pores", "Scale pores", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"poreDensity", 0.f, 1.f}}},
            {"finish", "Finish gloss", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"coat", 0.f, 0.4f}, {"coatRoughness", 0.3f, 0.12f}}},
            {"detailScale", "Scale frequency", "detail", true, "detailScale", 0.01f, 1000.f, 0.01f, "×", true, true, "", {}},
            {"surfaceSeed", "Pattern seed", "detail", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
        {
        },
    },
    {
        "leafSurface", 31, "Leaf · mesh surface",
        "Full UV surface, not an atlas. U=.5 is the midrib; V=0 is the stem and V=1 the tip. Veins and microscopic epidermal cells; no alpha cutout.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.12f}, {"sheen", 0.15f}, {"anisotropy", 0.f}, {"ior", 1.45f}, {"coatIor", 1.45f}},
        {{"color", "Leaf pigment"}, {"secondaryColor", "Vein pigment"}, {"tertiaryColor", "Aging pigment"}},
        {
            {"roughness", "Leaf Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.25f, 0.8f}}},
            {"veins", "Vein relief", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"veinRelief", 0.f, 1.5f}}},
            {"age", "Pigment aging", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"colorGradient", 0.f, 1.f}}},
            {"detailScale", "Vein pairs", "detail", true, "detailScale", 5.f, 20.f, 1.f, "", false, false, "", {}},
            {"lobing", "Cell wall lobing", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"cellLobing", 0.f, 1.f}}},
            {"stomata", "Stomatal detail", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"stomataDensity", 0.f, 1.f}}},
            {"cells", "Cell relief", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"cellRelief", 0.f, 1.f}}},
            {"cellScale", "Cell frequency", "detail", true, "cellScale", 20.f, 400.f, 0.01f, "×", true, false, "", {}},
            {"surfaceSeed", "Pattern seed", "detail", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
        {
        },
    },
    {
        "grassSurface", 32, "Grass blade · mesh surface",
        "One complete blade surface. U crosses its width, V runs root to tip. Longitudinal ribs and fine cells, no repeated blade silhouettes.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.06f}, {"sheen", 0.2f}, {"anisotropy", 0.f}, {"ior", 1.45f}, {"coatIor", 1.45f}},
        {{"color", "Base pigment"}, {"secondaryColor", "Tip pigment"}},
        {
            {"roughness", "Grass Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.4f, 0.9f}}},
            {"veins", "Rib relief", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"veinRelief", 0.f, 1.5f}}},
            {"gradient", "Tip gradient", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"colorGradient", 0.f, 1.f}}},
            {"plantRibs", "Rib count", "detail", true, "plantRibs", 3.f, 20.f, 1.f, "", false, false, "", {}},
            {"cells", "Cell relief", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"cellRelief", 0.f, 1.f}}},
            {"cellScale", "Cell frequency", "detail", true, "cellScale", 20.f, 400.f, 0.01f, "×", true, false, "", {}},
            {"surfaceSeed", "Pattern seed", "detail", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
        {
        },
    },
    {
        "petalSurface", 33, "Flower petal · mesh surface",
        "V runs from the flower throat to the petal tip. Basal/tip gradient, delicate striations, epidermal cells and optional spots. No atlas or cutout.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.04f}, {"sheen", 0.3f}, {"anisotropy", 0.f}, {"ior", 1.45f}, {"coatIor", 1.45f}},
        {{"color", "Petal tip"}, {"secondaryColor", "Petal base / throat"}, {"tertiaryColor", "Spot pigment"}},
        {
            {"roughness", "Petal Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.4f, 0.85f}}},
            {"gradient", "Gradient reach", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"colorGradient", 0.15f, 1.f}}},
            {"veins", "Striation relief", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"veinRelief", 0.f, 1.5f}}},
            {"spots", "Basal speckles", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"spotDensity", 0.f, 1.f}}},
            {"cells", "Cell relief", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"cellRelief", 0.f, 1.f}}},
            {"cellScale", "Cell frequency", "detail", true, "cellScale", 20.f, 400.f, 0.01f, "×", true, false, "", {}},
            {"surfaceSeed", "Pattern seed", "detail", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
        {
        },
    },
    {
        "berrySurface", 34, "Strawberry skin",
        "Analytic seed pockets with small raised seed centers. Normal/height relief, not separate seed meshes. U wraps the fruit.",
        false, false,
        {{"metalness", 0.f}, {"sheen", 0.f}, {"anisotropy", 0.f}, {"ior", 1.45f}, {"coatIor", 1.45f}},
        {{"color", "Berry pigment"}, {"secondaryColor", "Seed pigment"}},
        {
            {"roughness", "Skin Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.2f, 0.75f}}},
            {"relief", "Skin relief", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.f, 1.3f}}},
            {"wax", "Wax gloss", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"coat", 0.f, 0.35f}, {"coatRoughness", 0.3f, 0.12f}}},
            {"detailScale", "Pore / seed frequency", "detail", true, "detailScale", 8.f, 160.f, 0.01f, "×", true, false, "", {}},
            {"surfaceSeed", "Pattern seed", "detail", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
        {
        },
    },
    {
        "citrusSurface", 34, "Citrus peel",
        "Rounded oil-gland recesses in a finely pebbled peel. Full UV surface; U wraps around the fruit.",
        false, false,
        {{"metalness", 0.f}, {"sheen", 0.f}, {"anisotropy", 0.f}, {"ior", 1.45f}, {"coatIor", 1.45f}},
        {{"color", "Peel pigment"}},
        {
            {"roughness", "Skin Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.2f, 0.75f}}},
            {"relief", "Skin relief", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.f, 1.3f}}},
            {"wax", "Wax gloss", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"coat", 0.f, 0.35f}, {"coatRoughness", 0.3f, 0.12f}}},
            {"detailScale", "Pore / seed frequency", "detail", true, "detailScale", 8.f, 160.f, 0.01f, "×", true, false, "", {}},
            {"surfaceSeed", "Pattern seed", "detail", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
        {
        },
    },
    {
        "fruitSurface", 34, "Apple / waxed fruit skin",
        "Blush, lenticels and a waxed epidermis. U wraps around the fruit; V runs pole to pole. Full surface, not fruit silhouettes.",
        false, false,
        {{"metalness", 0.f}, {"sheen", 0.f}, {"anisotropy", 0.f}, {"ior", 1.45f}, {"coatIor", 1.45f}},
        {{"color", "Surface pigment"}, {"secondaryColor", "Secondary pigment"}, {"tertiaryColor", "Detail pigment"}},
        {
            {"roughness", "Skin Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.2f, 0.75f}}},
            {"relief", "Skin relief", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"grain", 0.f, 1.3f}}},
            {"wax", "Wax gloss", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"coat", 0.f, 0.35f}, {"coatRoughness", 0.3f, 0.12f}}},
            {"detailScale", "Pore / seed frequency", "detail", true, "detailScale", 8.f, 160.f, 0.01f, "×", true, false, "", {}},
            {"surfaceSeed", "Pattern seed", "detail", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
            {"gradient", "Blush variation", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"colorGradient", 0.f, 1.f}}},
            {"spots", "Lenticel coverage", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"spotDensity", 0.f, 1.f}}},
        },
        {
        },
    },
    {
        "cactusSurface", 35, "Cactus · mesh epidermis",
        "Apply to your cactus geometry. U wraps the body; V follows growth. Align rib columns to your mesh; areoles are relief only, not spines.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.07f}, {"sheen", 0.f}, {"anisotropy", 0.f}, {"ior", 1.45f}, {"coatIor", 1.45f}},
        {{"color", "Epidermis"}, {"secondaryColor", "Wax bloom"}, {"tertiaryColor", "Areole / cork"}},
        {
            {"roughness", "Cactus Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.3f, 0.85f}}},
            {"wax", "Wax bloom", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"colorGradient", 0.f, 1.f}}},
            {"veins", "Surface relief", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"veinRelief", 0.f, 1.5f}}},
            {"plantRibs", "Rib columns", "detail", true, "plantRibs", 4.f, 24.f, 1.f, "", false, false, "", {}},
            {"detailScale", "Areole rows", "detail", true, "detailScale", 4.f, 40.f, 1.f, "", false, false, "", {}},
            {"cells", "Cell relief", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"cellRelief", 0.f, 1.f}}},
            {"cellScale", "Cell frequency", "detail", true, "cellScale", 20.f, 400.f, 0.01f, "×", true, false, "", {}},
            {"surfaceSeed", "Pattern seed", "detail", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
        {
        },
    },
    {
        "stemSurface", 36, "Living plant stem",
        "Longitudinal fibers, young epidermis, cork pigment and lenticels. U wraps the stem; V follows its length. Full surface with no cutout.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.04f}, {"sheen", 0.1f}, {"anisotropy", 0.f}, {"ior", 1.45f}, {"coatIor", 1.45f}},
        {{"color", "Surface pigment"}, {"secondaryColor", "Secondary pigment"}, {"tertiaryColor", "Detail pigment"}},
        {
            {"roughness", "Stem Roughness", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"roughness", 0.4f, 0.9f}}},
            {"cork", "Cork maturity", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"colorGradient", 0.f, 1.f}}},
            {"veins", "Fiber relief", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"veinRelief", 0.f, 1.5f}}},
            {"spots", "Lenticel coverage", "finish", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"spotDensity", 0.f, 1.f}}},
            {"detailScale", "Fiber frequency", "detail", true, "detailScale", 5.f, 90.f, 0.01f, "×", true, false, "", {}},
            {"cells", "Cell relief", "detail", false, "", 0.f, 1.f, 0.01f, "", false, false, "", {{"cellRelief", 0.f, 1.f}}},
            {"cellScale", "Cell frequency", "detail", true, "cellScale", 20.f, 400.f, 0.01f, "×", true, false, "", {}},
            {"surfaceSeed", "Pattern seed", "detail", true, "surfaceSeed", 0.f, 9999.f, 1.f, "", false, false, "", {}},
        },
        {
        },
    },
    {
        "sand", 37, "Mineral sand grains",
        "Every seeded grain has a size, facet direction, roughness and dielectric reflection. Use Macro to inspect the grains. No automotive metalness or clearcoat.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"sheen", 0.f}, {"anisotropy", 0.f}, {"ior", 1.544f}},
        {{"color", "Sand body color"}, {"secondaryColor", "Pale mineral color"}},
        {
            {"grainSize", "Grain size", "grains", true, "grainSize", 0.0625f, 2.f, 0.01f, "mm", true, false, "", {}},
            {"grainSizeVariation", "Size variation", "grains", true, "grainSizeVariation", 0.f, 1.f, 0.01f, "", false, false, "", {}},
            {"grainRoughness", "Grain roughness", "finish", true, "grainRoughness", 0.04f, 1.f, 0.01f, "", false, false, "", {}},
            {"grainRoughnessVariation", "Roughness variation", "finish", true, "grainRoughnessVariation", 0.f, 1.f, 0.01f, "", false, false, "", {}},
            {"grainSpecular", "Grain specular", "finish", true, "grainSpecular", 0.f, 1.f, 0.01f, "", false, false, "", {}},
            {"grainSpecularVariation", "Specular variation", "finish", true, "grainSpecularVariation", 0.f, 1.f, 0.01f, "", false, false, "", {}},
            {"grainIOR", "Mineral IOR", "finish", true, "grainIOR", 1.3f, 2.2f, 0.01f, "", false, false, "", {}},
            {"grainRoundness", "Grain roundness", "grains", true, "grainRoundness", 0.f, 1.f, 0.01f, "", false, false, "", {}},
            {"grainTilt", "Facet tilt", "grains", true, "grainTilt", 0.f, 1.f, 0.01f, "", false, false, "", {}},
            {"grainRelief", "Grain relief", "grains", true, "grainRelief", 0.f, 1.f, 0.01f, "", false, false, "", {}},
            {"sandCoverage", "Grain coverage", "grains", true, "sandCoverage", 0.f, 1.f, 0.01f, "", false, false, "", {}},
            {"sandScale", "Grain repetition", "detail", true, "sandScale", 0.1f, 10.f, 0.01f, "×", true, false, "", {}},
            {"surfaceSeed", "Grain seed", "detail", true, "surfaceSeed", 0.f, 100000.f, 1.f, "", false, false, "", {}},
        },
        {
        },
    },
    {
        "layeredSand", 38, "Layered mineral sand",
        "Independent grain beds, composited top-to-bottom or exposed as sediment bands. Each bed owns its size, roughness, specular and relief; grains remain non-metallic.",
        false, false,
        {{"metalness", 0.f}, {"coat", 0.f}, {"sheen", 0.f}, {"anisotropy", 0.f}, {"ior", 1.544f}},
        {{"color", "Underlying bed color"}},
        {
            {"sandScale", "Grain repetition", "detail", true, "sandScale", 0.1f, 10.f, 0.01f, "×", true, false, "", {}},
            {"surfaceSeed", "Grain seed", "detail", true, "surfaceSeed", 0.f, 100000.f, 1.f, "", false, false, "", {}},
            {"sandBandFrequency", "Band repetition", "strata", true, "sandBandFrequency", 0.1f, 10.f, 0.01f, "×", true, false, "", {}},
            {"sandBandWarp", "Band waviness", "strata", true, "sandBandWarp", 0.f, 1.f, 0.01f, "", false, false, "", {}},
            {"sandBandAngle", "Band direction", "strata", true, "sandBandAngle", -180.f, 180.f, 1.f, "°", false, false, "", {}},
        },
        {
        },
    },
};

} // namespace

const MaterialRecipeProfile *QueryMaterialRecipeProfile(std::string_view RecipeId)
{
    for (const auto &Profile : Profiles)
        if (Profile.Identity == RecipeId) return &Profile;
    return nullptr;
}

void AppendMaterialRecipeControls(std::vector<MaterialRecipeControl> &Controls,
    const std::vector<MaterialRecipeProfileControl> &Source)
{
    for (const auto &Entry : Source)
    {
        MaterialRecipeControl Control;
        Control.Id = Entry.Id;
        Control.Caption = Entry.Label;
        Control.Group = Entry.Group;
        Control.Direct = Entry.Direct;
        Control.Key = Entry.Key;
        Control.Minimum = Entry.Minimum;
        Control.Maximum = Entry.Maximum;
        Control.Step = Entry.Step;
        Control.Unit = Entry.Unit;
        Control.Logarithmic = Entry.Logarithmic;
        Control.Extend = Entry.Extend;
        Control.Hint = Entry.Hint;
        Control.Targets = Entry.Targets;
        Controls.push_back(std::move(Control));
    }
}

std::vector<MaterialRecipeControl> QueryMaterialRecipeControls(const MaterialCatalogueEntry &Entry, bool ScratchesEnabled)
{
    std::vector<MaterialRecipeControl> Controls;
    const auto *Profile = QueryMaterialRecipeProfile(Entry.Recipe.RecipeId);
    if (!Profile) return Controls;
    Controls.reserve(Profile->Controls.size() + Profile->ScratchControls.size());
    AppendMaterialRecipeControls(Controls, Profile->Controls);
    // 📝 The scratch overlay rides on top of the base recipe exactly like the HTML: enabling it
    // appends the study field controls under their own independent names and ranges, and the
    // direction control is renamed so finish and relief angles never collide.
    if (ScratchesEnabled) AppendMaterialRecipeControls(Controls, Profile->ScratchControls);
    return Controls;
}

} // namespace Frontier
