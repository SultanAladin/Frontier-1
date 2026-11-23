//============================================================================================================================================
//                                                             RENDERSETTINGS.JS
//============================================================================================================================================
// 🎛️ Per-camera render settings. Engines, shadow-map algorithms, SDF penumbra modes, quality presets and clamping are translated from
// the FrontierWorldMirror gallery (unassignedinbox/Frontier, branch arena/28ada475-frontier, ShowcaseGalleryScene.js). Each renderer
// (PBR raster, SDF, ray traced, MatCap) keeps its own shadow algorithm and its own settings, so switching engines never loses another
// renderer's setup. Every camera row stores its own copy in Values["Render Settings"]. Browser authoring only: nothing is sent to a
// renderer.

export const RenderSettingsKey = "Render Settings";

export const RENDER_ENGINES = [
  { id: "matcap", ordinal: 0, label: "0. MatCap (Studio Normal)", short: "MatCap", pick: "MatCap studio", desc: "Studio MatCap normal/roughness shading. No GI, no SDF." },
  { id: "openpbr_direct", ordinal: 1, label: "1. OpenPBR Direct", short: "OpenPBR", pick: "OpenPBR direct", desc: "Direct OpenPBR with LTC area lights and the shadow lab. No GI, no SDF." },
  { id: "gtx_sdf_gi", ordinal: 2, label: "2. RealTime GI · GTX Low-End", short: "GTX GI", pick: "GTX real-time GI", desc: "Hybrid cone penumbra with multi-bounce cone GI." },
  { id: "highend_bvh4_gi", ordinal: 3, label: "3. RealTime GI · High-End BVH4", short: "BVH4 GI", pick: "BVH4 real-time GI", desc: "BVH4 two-bounce GI with Heitz projected-disk VNDF reflections. No SDF." },
  { id: "pt_realtime", ordinal: 4, label: "4. Path Tracer · GameRealtime", short: "PT game", pick: "Path tracer · game", desc: "Deterministic path tracer with blue-noise R2 sampling and reconstruction." },
  { id: "pt_final", ordinal: 5, label: "5. Path Tracer · SceneFinalRender", short: "PT final", pick: "Path tracer · final", desc: "Deterministic path tracer with shared Sobol sampling to exact convergence." },
];

// The four renderers. Ray traced covers the BVH4 engine and both path tracers (RayTracer picks which).
export const RENDERERS = [
  { id: "pbr", label: "PBR", title: "PBR raster", engines: ["openpbr_direct"] },
  { id: "sdf", label: "SDF", title: "SDF tracing", engines: ["gtx_sdf_gi"] },
  { id: "ray", label: "Ray traced", title: "Ray tracing", engines: ["highend_bvh4_gi", "pt_realtime", "pt_final"] },
  { id: "matcap", label: "MatCap", title: "MatCap", engines: ["matcap"] },
];
export const RendererOf = (Engine) => RENDERERS.find((Each) => Each.engines.includes(Engine)) || RENDERERS[0];

// PBR raster shadows: the source's shadow-map algorithms (ShadowAlgorithmLab, without the BVH entry, which belongs to ray tracing).
export const PBR_SHADOWS = [
  { id: "contact_pcss", label: "Contact + PCSS (No SDF)", pick: "Contact PCSS", desc: "Screen/tight contact rays plus Vogel-disk PCSS. No SDF." },
  { id: "pcss", label: "Shadow Map · PCSS + Filter", pick: "PCSS Plane-Aware", desc: "Vogel blocker search, geometric penumbra and the plane filter." },
  { id: "smrt", label: "SMRT · Depth Ray Trace", pick: "Shadow Ray March", desc: "EEVEE-Next style quadratic ray marching through the depth map." },
  { id: "pcf", label: "Shadow Map · 3×3 PCF", pick: "PCF Bilinear", desc: "Bilinear-weighted 3 × 3 percentage-closer filtering." },
];

// SDF shadows: the source's distance-field penumbra modes.
export const SDF_SHADOWS = [
  { id: "point_sdf", ordinal: 0, label: "0. Point-Sample (No Penumbra)", pick: "Point SDF", desc: "Crisp point-centroid occlusion with no penumbra spread." },
  { id: "analytic_cone", ordinal: 1, label: "1. Analytic Cone Penumbra", pick: "Analytic Cone", desc: "Closed-form solid-angle cone penumbra across area emitters." },
  { id: "voxel_clipmap", ordinal: 2, label: "2. 3D Voxel Clipmap Penumbra", pick: "Voxel Clipmap", desc: "Stratified 3D clipmap, cone-traced area penumbra." },
  { id: "voxel_cone_gi", ordinal: 3, label: "3. 3D Voxel Penumbra + Cone GI", pick: "Voxel + Cone GI", desc: "Clipmap penumbra coupled with multi-cone indirect bounce GI." },
  { id: "hybrid_sobol_cap", ordinal: 4, label: "4. Hybrid + Sobol Spherical Cap", pick: "Hybrid Sobol Cap", desc: "Owen-scrambled Sobol spherical-cap penumbra integration." },
  { id: "sdf_mc_ref", ordinal: 5, label: "5. Monte Carlo PT Ref (96 SPP)", pick: "SDF MC Ref 96 spp", desc: "High-sample Monte Carlo reference penumbra." },
];

// Ray-traced shadows: the source's BVH entry ("stochastic Vogel-disk and rectangular-area visibility") split into its two light
// shapes, so each can be chosen.
export const RAY_SHADOWS = [
  { id: "bvh_disk", pick: "WideTree RayQuery · disk", desc: "Stochastic Vogel-disk visibility through the BVH; suits round and point lights." },
  { id: "bvh_area", pick: "WideTree RayQuery · area", desc: "Rectangular-area visibility through the BVH; suits panel and strip lights." },
];

// MatCap shadows: editor additions (the source MatCap engine draws no shadows).
export const MATCAP_SHADOWS = [
  { id: "none", pick: "None", desc: "Shape only, no shadow." },
  { id: "cavity", pick: "Cavity occlusion", desc: "Darkens creases and grooves." },
  { id: "drop", pick: "Studio drop", desc: "A soft shadow under the object on an implied floor." },
  { id: "contact", pick: "Screen contact", desc: "Short screen-space rays towards the studio light." },
];
export const MATCAP_CHANNELS = [
  { id: "shaded", label: "Shaded", desc: "Clay shading lit by the studio light, with rim and cavity." },
  { id: "normal", label: "Normals", desc: "View-space normals as colour." },
  { id: "roughness", label: "Roughness", desc: "The specular lobe in grey, to read surface finish." },
  { id: "cavity", label: "Cavity", desc: "Creases and grooves only." },
];

export const QUALITY_PRESETS = [
  { id: 0, label: "Low", bvhRays: 1, smrtRays: 2, pcssTaps: 8, giSamples: 4, giCones: 4, reflRays: 1, sdfSteps: 16 },
  { id: 1, label: "Medium (GTX)", bvhRays: 2, smrtRays: 4, pcssTaps: 16, giSamples: 6, giCones: 6, reflRays: 2, sdfSteps: 24 },
  { id: 2, label: "High", bvhRays: 4, smrtRays: 6, pcssTaps: 24, giSamples: 10, giCones: 8, reflRays: 4, sdfSteps: 32 },
  { id: 3, label: "Ultra", bvhRays: 8, smrtRays: 8, pcssTaps: 32, giSamples: 14, giCones: 10, reflRays: 6, sdfSteps: 48 },
  // 📝 Reference is an editor tier; the specification lists Low to Ultra. Kept for the user to decide.
  { id: 4, label: "Reference", note: "Not in the Project Zero specification (Low to Ultra).", bvhRays: 16, smrtRays: 12, pcssTaps: 48, giSamples: 20, giCones: 14, reflRays: 8, sdfSteps: 64 },
];

export const RENDER_SCALES = [50, 75, 100, 125, 150, 200];

// Follow-up 48 specification additions (§10): reflection source, trace resolution and XeGTAO.
export const REFLECTION_SOURCES = [
  { id: "widetree", pick: "WideTree", desc: "Traced reflections through the WideTree BVH." },
  { id: "sky", pick: "Sky", desc: "Sky and environment only; no traced reflections." },
];
export const TRACE_RESOLUTIONS = [
  { id: 1, pick: "Full" },
  { id: 0.5, pick: "½" },
  { id: 0.25, pick: "¼" },
];

// Follow-up 46: ambient occlusion belongs to each renderer. PBR raster keeps XeGTAO (screen-space horizon slices); SDF and ray tracing
// get their own methods, stored under their own keys so switching renderers keeps each setup.
export const SDF_AO = [
  { id: "sdf_multitap", pick: "Multi-tap SDF AO", desc: "Distance samples stepped along the normal; cheap and view independent." },
  { id: "sdf_cone", pick: "Cone-traced SDF AO", desc: "A few wide cones marched through the distance field; soft, large-scale occlusion." },
  { id: "sdf_clipmap", pick: "Voxel clipmap AO", desc: "Occlusion read from the 3D voxel clipmap the penumbra modes already build." },
];
export const RAY_AO = [
  { id: "rtao", pick: "RTAO", desc: "Cosine-weighted short rays through the BVH, then denoised." },
  { id: "rtao_bent", pick: "RTAO + bent normal", desc: "RTAO that also keeps the least occluded direction, for specular occlusion." },
  { id: "traced_gi", pick: "From traced GI", desc: "No separate pass: occlusion falls out of the traced GI; suits the path tracers." },
];

// CanonicalEditorCamera.ExposureGain in the source scene is 1.35; the counts are the source's Medium (GTX) tier.
export const CanonicalRenderSettings = {
  RenderEngine: "gtx_sdf_gi",
  RayTracer: "highend_bvh4_gi",
  ExposureGain: 1.35,
  RenderScale: 100,
  AntiAliasing: true,

  PbrShadow: "contact_pcss",
  PbrShadowQuality: 1,
  PbrContactRays: 2,
  PbrPcssTaps: 16,
  PbrSmrtRays: 4,
  PbrMapRes: 2048,
  PbrPlaneFilter: true,
  PbrHalfRes: false,
  PbrSoftness: 1.0,
  PbrEmitters: "shadowed",
  PbrAreaLights: true,
  PbrFill: 1.15,

  SdfShadow: "voxel_cone_gi",
  SdfShadowQuality: 1,
  SdfSteps: 24,
  SdfSoftness: 1.0,
  SdfGi: true,
  SdfGiQuality: 1,
  SdfCones: 6,
  SdfGiIntensity: 1.25,
  SdfFill: 1.15,

  RayShadow: "bvh_disk",
  RayShadowQuality: 1,
  RayShadowRays: 2,
  RaySoftness: 1.0,
  RayGi: true,
  RayGiQuality: 1,
  RayGiSamples: 6,
  RayGiIntensity: 1.25,
  RayReflections: true,
  RayReflectionQuality: 1,
  RayReflectionRays: 2,
  RayReflectionBounces: 2,
  RayFill: 1.15,

  MatcapShadow: "drop",
  MatcapShadowStrength: 0.6,
  MatcapShadowSoftness: 1.0,
  MatcapChannel: "shaded",
  MatcapLight: 40,
  MatcapRim: 0.35,
  MatcapCavity: 0.5,
  MatcapWire: false,
  MatcapTint: "#cfc6ba",

  ReflectionSource: "widetree",
  TraceResolution: 1,
  XeGtao: true,
  XeGtaoStrength: 1.0,
  XeGtaoRadius: 0.5,
  XeGtaoSlices: 2,

  SdfAo: true,
  SdfAoMethod: "sdf_cone",
  SdfAoTaps: 5,
  SdfAoCones: 4,
  SdfAoRadius: 1.0,
  SdfAoStrength: 1.0,

  RayAo: true,
  RayAoMethod: "rtao",
  RayAoRays: 2,
  RayAoRadius: 1.0,
  RayAoStrength: 1.0,
};

const Bounded = (Amount, Minimum, Maximum, Fallback, Whole = false) => {
  const Numeric = Number(Amount);
  const Safe = Number.isFinite(Numeric) ? Numeric : Fallback;
  const Clamped = Math.max(Minimum, Math.min(Maximum, Safe));
  return Whole ? Math.round(Clamped) : Clamped;
};
const Known = (List, Id, Fallback) => (List.some((Option) => Option.id === Id) ? Id : Fallback);

// Follow-up 37–41 drafts stored one shared set of shadow / GI / reflection keys. They map onto the renderer that used them.
function Upgrade(Stored) {
  if (!Stored || Stored.PbrShadow !== undefined || Stored.ShadowAlgorithm === undefined) return Stored || {};
  const Old = Stored, Next = { ...Old };
  if (Old.ShadowAlgorithm === "bvh") Next.RayShadowRays = Old.ShadowRays;
  else Object.assign(Next, { PbrShadow: Old.ShadowAlgorithm, PbrContactRays: Old.ShadowRays });
  Object.assign(Next, {
    PbrShadowQuality: Old.ShadowQuality, PbrPcssTaps: Old.PcssTaps, PbrSmrtRays: Old.SmrtRays, PbrMapRes: Old.ShadowMapRes,
    PbrPlaneFilter: Old.PlaneFilter, PbrHalfRes: Old.HalfResShadows, PbrSoftness: Old.ShadowSoftness, PbrEmitters: Old.EmissiveShadowMode,
    PbrFill: Old.FillBoost, SdfFill: Old.FillBoost, RayFill: Old.FillBoost, SdfShadow: Old.SdfPenumbraMode,
    SdfCones: Old.GiCones, SdfGiIntensity: Old.GiIntensity, SdfGi: Old.GiAlgorithm === undefined ? undefined : Old.GiAlgorithm === "sdf",
    RayGiSamples: Old.GiSamples, RayGiIntensity: Old.GiIntensity, RayGi: Old.GiAlgorithm === undefined ? undefined : Old.GiAlgorithm === "bvh",
    RayReflections: Old.ReflectionAlgorithm === undefined ? undefined : Old.ReflectionAlgorithm === "bvh",
    RayReflectionQuality: Old.ReflectionQuality, RayReflectionRays: Old.ReflectionRays, RayReflectionBounces: Old.ReflectionBounces,
  });
  if (RENDERERS[2].engines.includes(Old.RenderEngine)) Next.RayTracer = Old.RenderEngine;
  return Next;
}

// Source behaviour: unknown ids fall back to the defaults, every count and gain is clamped to the lab's range.
export function ResolveRenderSettings(Stored) {
  const C = CanonicalRenderSettings;
  const M = { ...C };
  for (const [Key, Value] of Object.entries(Upgrade(Stored && typeof Stored === "object" ? Stored : {}))) if (Value !== undefined && Key in C) M[Key] = Value;
  const Flag = (Key) => (typeof M[Key] === "boolean" ? M[Key] : C[Key]);
  const Tier = (Key) => Bounded(M[Key], 0, 4, C[Key], true);
  return {
    RenderEngine: Known(RENDER_ENGINES, M.RenderEngine, C.RenderEngine),
    RayTracer: RENDERERS[2].engines.includes(M.RayTracer) ? M.RayTracer : C.RayTracer,
    ExposureGain: Bounded(M.ExposureGain, 0.2, 3.5, C.ExposureGain),
    RenderScale: RENDER_SCALES.includes(Number(M.RenderScale)) ? Number(M.RenderScale) : 100,
    AntiAliasing: Flag("AntiAliasing"),

    PbrShadow: Known(PBR_SHADOWS, M.PbrShadow, C.PbrShadow),
    PbrShadowQuality: Tier("PbrShadowQuality"),
    PbrContactRays: Bounded(M.PbrContactRays, 1, 16, C.PbrContactRays, true),
    PbrPcssTaps: Bounded(M.PbrPcssTaps, 8, 48, C.PbrPcssTaps, true),
    PbrSmrtRays: Bounded(M.PbrSmrtRays, 2, 12, C.PbrSmrtRays, true),
    PbrMapRes: Number(M.PbrMapRes) === 1024 ? 1024 : 2048,
    PbrPlaneFilter: Flag("PbrPlaneFilter"),
    PbrHalfRes: Flag("PbrHalfRes"),
    PbrSoftness: Bounded(M.PbrSoftness, 0.25, 2.5, C.PbrSoftness),
    PbrEmitters: M.PbrEmitters === "unshadowed" ? "unshadowed" : "shadowed",
    PbrAreaLights: Flag("PbrAreaLights"),
    PbrFill: Bounded(M.PbrFill, 0.2, 2.5, C.PbrFill),

    SdfShadow: Known(SDF_SHADOWS, M.SdfShadow, C.SdfShadow),
    SdfShadowQuality: Tier("SdfShadowQuality"),
    SdfSteps: Bounded(M.SdfSteps, 8, 96, C.SdfSteps, true),
    SdfSoftness: Bounded(M.SdfSoftness, 0.25, 2.5, C.SdfSoftness),
    SdfGi: Flag("SdfGi"),
    SdfGiQuality: Tier("SdfGiQuality"),
    SdfCones: Bounded(M.SdfCones, 4, 14, C.SdfCones, true),
    SdfGiIntensity: Bounded(M.SdfGiIntensity, 0, 2.5, C.SdfGiIntensity),
    SdfFill: Bounded(M.SdfFill, 0.2, 2.5, C.SdfFill),

    RayShadow: Known(RAY_SHADOWS, M.RayShadow, C.RayShadow),
    RayShadowQuality: Tier("RayShadowQuality"),
    RayShadowRays: Bounded(M.RayShadowRays, 1, 16, C.RayShadowRays, true),
    RaySoftness: Bounded(M.RaySoftness, 0.25, 2.5, C.RaySoftness),
    RayGi: Flag("RayGi"),
    RayGiQuality: Tier("RayGiQuality"),
    RayGiSamples: Bounded(M.RayGiSamples, 2, 20, C.RayGiSamples, true),
    RayGiIntensity: Bounded(M.RayGiIntensity, 0, 2.5, C.RayGiIntensity),
    RayReflections: Flag("RayReflections"),
    RayReflectionQuality: Tier("RayReflectionQuality"),
    RayReflectionRays: Bounded(M.RayReflectionRays, 1, 8, C.RayReflectionRays, true),
    RayReflectionBounces: Bounded(M.RayReflectionBounces, 1, 3, C.RayReflectionBounces, true),
    RayFill: Bounded(M.RayFill, 0.2, 2.5, C.RayFill),

    MatcapShadow: Known(MATCAP_SHADOWS, M.MatcapShadow, C.MatcapShadow),
    MatcapShadowStrength: Bounded(M.MatcapShadowStrength, 0, 1, C.MatcapShadowStrength),
    MatcapShadowSoftness: Bounded(M.MatcapShadowSoftness, 0.25, 2.5, C.MatcapShadowSoftness),
    MatcapChannel: Known(MATCAP_CHANNELS, M.MatcapChannel, C.MatcapChannel),
    MatcapLight: Bounded(M.MatcapLight, 0, 360, C.MatcapLight, true),
    MatcapRim: Bounded(M.MatcapRim, 0, 1, C.MatcapRim),
    MatcapCavity: Bounded(M.MatcapCavity, 0, 1, C.MatcapCavity),
    MatcapWire: Flag("MatcapWire"),
    MatcapTint: /^#[0-9a-f]{6}$/i.test(M.MatcapTint) ? M.MatcapTint : C.MatcapTint,

    ReflectionSource: Known(REFLECTION_SOURCES, M.ReflectionSource, C.ReflectionSource),
    TraceResolution: TRACE_RESOLUTIONS.some((Option) => Option.id === Number(M.TraceResolution)) ? Number(M.TraceResolution) : C.TraceResolution,
    XeGtao: Flag("XeGtao"),
    XeGtaoStrength: Bounded(M.XeGtaoStrength, 0, 2, C.XeGtaoStrength),
    XeGtaoRadius: Bounded(M.XeGtaoRadius, 0.1, 3, C.XeGtaoRadius),
    XeGtaoSlices: Bounded(M.XeGtaoSlices, 1, 9, C.XeGtaoSlices, true),

    SdfAo: Flag("SdfAo"),
    SdfAoMethod: Known(SDF_AO, M.SdfAoMethod, C.SdfAoMethod),
    SdfAoTaps: Bounded(M.SdfAoTaps, 3, 12, C.SdfAoTaps, true),
    SdfAoCones: Bounded(M.SdfAoCones, 1, 9, C.SdfAoCones, true),
    SdfAoRadius: Bounded(M.SdfAoRadius, 0.1, 4, C.SdfAoRadius),
    SdfAoStrength: Bounded(M.SdfAoStrength, 0, 2, C.SdfAoStrength),

    RayAo: Flag("RayAo"),
    RayAoMethod: Known(RAY_AO, M.RayAoMethod, C.RayAoMethod),
    RayAoRays: Bounded(M.RayAoRays, 1, 16, C.RayAoRays, true),
    RayAoRadius: Bounded(M.RayAoRadius, 0.1, 5, C.RayAoRadius),
    RayAoStrength: Bounded(M.RayAoStrength, 0, 2, C.RayAoStrength),
  };
}

// Preset fan-out, as in the source inspector: one tier choice rewrites the counts that tier controls, per renderer.
const Preset = (Tier) => QUALITY_PRESETS[Tier] || QUALITY_PRESETS[1];
export const PbrShadowPatch = (Tier) => ({ PbrShadowQuality: Preset(Tier).id, PbrContactRays: Preset(Tier).bvhRays, PbrPcssTaps: Preset(Tier).pcssTaps, PbrSmrtRays: Preset(Tier).smrtRays, PbrMapRes: Preset(Tier).id === 0 ? 1024 : 2048 });
export const SdfShadowPatch = (Tier) => ({ SdfShadowQuality: Preset(Tier).id, SdfSteps: Preset(Tier).sdfSteps });
export const SdfGiPatch = (Tier) => ({ SdfGiQuality: Preset(Tier).id, SdfCones: Preset(Tier).giCones });
export const RayShadowPatch = (Tier) => ({ RayShadowQuality: Preset(Tier).id, RayShadowRays: Preset(Tier).bvhRays });
export const RayGiPatch = (Tier) => ({ RayGiQuality: Preset(Tier).id, RayGiSamples: Preset(Tier).giSamples });
export const RayReflectionPatch = (Tier) => ({ RayReflectionQuality: Preset(Tier).id, RayReflectionRays: Preset(Tier).reflRays, RayReflectionBounces: Preset(Tier).id >= 2 ? 3 : Preset(Tier).id >= 1 ? 2 : 1 });
export const MasterQualityPatch = (Tier) => ({ ...PbrShadowPatch(Tier), ...SdfShadowPatch(Tier), ...SdfGiPatch(Tier), ...RayShadowPatch(Tier), ...RayGiPatch(Tier), ...RayReflectionPatch(Tier) });

// Each renderer's tier keys; the global tier is the one they all share, or -1 ("custom").
const TierKeys = ["PbrShadowQuality", "SdfShadowQuality", "SdfGiQuality", "RayShadowQuality", "RayGiQuality", "RayReflectionQuality"];
export const MasterTier = (Settings) => (TierKeys.every((Key) => Settings[Key] === Settings[TierKeys[0]]) ? Settings[TierKeys[0]] : -1);

// Picking a renderer picks its engine; ray tracing remembers which tracer it last used.
export function EnginePatch(Id) {
  return RENDERERS[2].engines.includes(Id) ? { RenderEngine: Id, RayTracer: Id } : { RenderEngine: Id };
}
export const RendererPatch = (Renderer, Settings) => EnginePatch(Renderer === "ray" ? Settings.RayTracer : RENDERERS.find((Each) => Each.id === Renderer).engines[0]);

// Per-pixel work for the active renderer (shadows per light).
export function SampleBudget(Settings) {
  const Renderer = RendererOf(Settings.RenderEngine).id;
  let Shadow = 0, Gi = 0, Reflection = 0;
  if (Renderer === "pbr") {
    Shadow = { contact_pcss: Settings.PbrContactRays + Settings.PbrPcssTaps, pcss: Settings.PbrPcssTaps, smrt: Settings.PbrSmrtRays, pcf: 9 }[Settings.PbrShadow];
    if (Settings.PbrHalfRes) Shadow = Shadow / 4;
  } else if (Renderer === "sdf") {
    Shadow = Settings.SdfSteps;
    Gi = Settings.SdfGi ? Settings.SdfCones : 0;
  } else if (Renderer === "ray") {
    Shadow = Settings.RayShadowRays;
    Gi = Settings.RayGi ? Settings.RayGiSamples * 2 : 0;
    Reflection = Settings.RayReflections ? Settings.RayReflectionRays * Settings.RayReflectionBounces : 0;
  } else Shadow = Settings.MatcapShadow === "contact" ? 8 : Settings.MatcapShadow === "none" ? 0 : 1;
  const Scale = (Settings.RenderScale / 100) ** 2;
  return { Shadow: +Shadow.toFixed(2), Gi, Reflection, Scale, Total: (Shadow + Gi + Reflection) * Scale };
}

export const Find = (List, Id) => List.find((Option) => Option.id === Id) || List[0];

// The active renderer's ambient occlusion: its switch and strength keys and a short line. MatCap has none (its cavity term is shape only).
export function OcclusionOf(Settings) {
  const Renderer = RendererOf(Settings.RenderEngine).id;
  if (Renderer === "pbr") return { Renderer, Label: "XeGTAO", Switch: "XeGtao", Strength: "XeGtaoStrength", Line: Settings.XeGtao ? `XeGTAO · ${Settings.XeGtaoSlices} slices · ${Settings.XeGtaoRadius.toFixed(2)} m` : "Off" };
  if (Renderer === "sdf") {
    const Method = Find(SDF_AO, Settings.SdfAoMethod);
    const Count = Method.id === "sdf_multitap" ? `${Settings.SdfAoTaps} taps` : Method.id === "sdf_cone" ? `${Settings.SdfAoCones} cones` : "clipmap";
    return { Renderer, Label: Method.pick, Switch: "SdfAo", Strength: "SdfAoStrength", Line: Settings.SdfAo ? `${Method.pick} · ${Count}` : "Off" };
  }
  if (Renderer === "ray") {
    const Method = Find(RAY_AO, Settings.RayAoMethod);
    return { Renderer, Label: Method.pick, Switch: "RayAo", Strength: "RayAoStrength", Line: Settings.RayAo ? (Method.id === "traced_gi" ? Method.pick : `${Method.pick} · ${Settings.RayAoRays} rays`) : "Off" };
  }
  return null;
}

// Short "shadows · GI · reflections" lines for the active renderer.
export function RendererLines(Settings) {
  const Occlusion = OcclusionOf(Settings);
  const Lines = RendererLinesWithoutOcclusion(Settings);
  return Occlusion ? [...Lines, ["AO", Occlusion.Line]] : Lines;
}
function RendererLinesWithoutOcclusion(Settings) {
  const Renderer = RendererOf(Settings.RenderEngine).id;
  if (Renderer === "pbr") return [["Shadows", `${Find(PBR_SHADOWS, Settings.PbrShadow).pick} · ${Settings.PbrMapRes}²`], ["GI", "Ambient fill"], ["Reflections", "Sky / env"]];
  if (Renderer === "sdf") return [["Shadows", Find(SDF_SHADOWS, Settings.SdfShadow).pick], ["GI", Settings.SdfGi ? `${Settings.SdfCones} SDF cones` : "Ambient fill"], ["Reflections", "Sky / env"]];
  if (Renderer === "ray")
    return [["Shadows", `${Find(RAY_SHADOWS, Settings.RayShadow).pick} · ${Settings.RayShadowRays} rays`], ["GI", Settings.RayGi ? `${Settings.RayGiSamples} samples` : "Ambient fill"], ["Reflections", Settings.RayReflections ? `${Settings.RayReflectionRays} × ${Settings.RayReflectionBounces} bounces` : "Sky / env"]];
  return [["Shadows", Find(MATCAP_SHADOWS, Settings.MatcapShadow).pick], ["Shading", Find(MATCAP_CHANNELS, Settings.MatcapChannel).label], ["Light", `${Settings.MatcapLight}°`]];
}
