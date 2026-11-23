//============================================================================================================================================
//                                                          RENDERCOMPONENTS.JSX
//============================================================================================================================================
// 🎛️ The camera's Render Engine component. Every camera (Editor, Cine and any placed from Construct) has one component row; its
// inspector has five tabs. Engine picks the renderer and holds the shared Quality and Output cards. PBR, SDF, Ray traced and MatCap
// each hold that renderer's own shadow algorithm and settings, so switching renderer never loses another renderer's setup. All tabs
// edit the camera's Values["Render Settings"] (RenderSettings.js). Browser authoring only: no renderer is driven.

import React, { useState } from "react";
import {
  RenderSettingsKey,
  ResolveRenderSettings,
  CanonicalRenderSettings,
  RENDER_ENGINES,
  RENDERERS,
  RendererOf,
  RendererPatch,
  EnginePatch,
  PBR_SHADOWS,
  SDF_SHADOWS,
  RAY_SHADOWS,
  MATCAP_SHADOWS,
  MATCAP_CHANNELS,
  QUALITY_PRESETS,
  RENDER_SCALES,
  REFLECTION_SOURCES,
  TRACE_RESOLUTIONS,
  PbrShadowPatch,
  SdfShadowPatch,
  SdfGiPatch,
  RayShadowPatch,
  RayGiPatch,
  RayReflectionPatch,
  MasterQualityPatch,
  MasterTier,
  SampleBudget,
  RendererLines,
  Find,
  SDF_AO,
  RAY_AO,
} from "./RenderSettings.js";
import { PenumbraStudy, HemisphereStudy, BounceStudy, ScaleStudy, KernelStudy, SdfTraceStudy, MatcapBall, BudgetBar, StudioShadowStudy, OcclusionStudy } from "./RenderStudies.jsx";
import "./RenderSettings.css";

const Tones = { Shadow: "#7ab8ff", Gi: "#7be0ad", Reflection: "#c5a3ff", Occlusion: "#e0b07b" };

// Component, baked icon, description. One component per camera.
export const RenderComponentTypes = [["Render Engine", "render-settings", "Renderer, per-renderer shadows, quality and output"]];
// Components earlier drafts split the settings into (Follow-up 41); saved scenes drop them (ObjectComponents.jsx).
export const RetiredRenderComponents = ["MatCap", "Shadow Maps", "SDF Tracing", "Ray Tracing"];

const Tabs = [["engine", "Engine"], ...RENDERERS.map((Renderer) => [Renderer.id, Renderer.label])];

// The renderer that draws with the current settings, per renderer id.
export const RenderUse = (Settings) => Object.fromEntries(RENDERERS.map((Renderer) => [Renderer.id, RendererOf(Settings.RenderEngine).id === Renderer.id]));

const TierName = (Settings) => {
  const Tier = MasterTier(Settings);
  return Tier < 0 ? "Custom tiers" : QUALITY_PRESETS[Tier].label.replace(" (GTX)", "");
};

// Outliner value line.
export function RenderSummary(Component, Values = {}) {
  const Settings = ResolveRenderSettings(Values[RenderSettingsKey]);
  const Renderer = RendererOf(Settings.RenderEngine);
  const Engine = Renderer.id === "ray" ? ` (${Find(RENDER_ENGINES, Settings.RenderEngine).short})` : "";
  return `${Renderer.label}${Engine} · ${TierName(Settings)} · ${Settings.RenderScale} %`;
}

// The common shape the studies read, per renderer.
const PbrStudy = (S) => ({ ShadowAlgorithm: S.PbrShadow, ShadowSoftness: S.PbrSoftness, ShadowMapRes: S.PbrMapRes, HalfResShadows: S.PbrHalfRes, ShadowRays: S.PbrContactRays, SmrtRays: S.PbrSmrtRays, PcssTaps: S.PbrPcssTaps });
const SdfGiStudy = (S) => ({ GiAlgorithm: S.SdfGi ? "sdf" : "off", GiCones: S.SdfCones, GiSamples: 0, FillBoost: S.SdfFill });
const RayShadowStudy = (S) => ({ ShadowAlgorithm: "bvh", ShadowSoftness: S.RaySoftness, ShadowRays: S.RayShadowRays, ShadowMapRes: 2048, HalfResShadows: false });
const RayGiStudy = (S) => ({ GiAlgorithm: S.RayGi ? "bvh" : "off", GiSamples: S.RayGiSamples, GiCones: 0, FillBoost: S.RayFill });
const RayBounceStudy = (S) => ({ ReflectionAlgorithm: S.RayReflections ? "bvh" : "env", ReflectionRays: S.RayReflectionRays, ReflectionBounces: S.RayReflectionBounces });

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHARED PIECES
//------------------------------------------------------------------------------------------------------------------------

// Radio tiles: a small grid of choices with a short line each.
function Tiles({ Label, Options, Active, Choose, Columns = 2 }) {
  return (
    <div className="rc-tiles" role="radiogroup" aria-label={Label} style={{ "--rc-columns": Columns }}>
      {Options.map((Option) => (
        <button key={Option.id} type="button" role="radio" aria-checked={Option.id === Active} onClick={() => Choose(Option.id)}>
          <b>{Option.pick || Option.label}</b>
          {Option.line && <small>{Option.line}</small>}
        </button>
      ))}
    </div>
  );
}

// Pill segments for a short ordered choice.
function Segments({ Label, Options, Active, Choose }) {
  return (
    <div className="rc-segments" role="radiogroup" aria-label={Label}>
      {Options.map(([Id, Text]) => (
        <button key={Id} type="button" role="radio" aria-checked={Id === Active} onClick={() => Choose(Id)}>
          {Text}
        </button>
      ))}
    </div>
  );
}

// Top of each renderer tab: whether this renderer is drawing, with a one-click switch to it.
function ActiveStrip({ Renderer, Settings, Assign }) {
  const On = RenderUse(Settings)[Renderer.id];
  const Current = RendererOf(Settings.RenderEngine);
  return (
    <div className={"rc-use" + (On ? " on" : "")} data-in-use={On}>
      <i />
      <span>
        <b>{On ? "Rendering" : "Not rendering"}</b>
        <small>{On ? `This camera renders with ${Renderer.title}` : `This camera renders with ${Current.title}; these settings are kept`}</small>
      </span>
      {!On && (
        <button type="button" onClick={() => Assign(RendererPatch(Renderer.id, Settings))}>
          Use {Renderer.label}
        </button>
      )}
    </div>
  );
}

// The last tab opened per camera, so reselecting a camera returns to it.
const OpenTab = new Map();

//------------------------------------------------------------------------------------------------------------------------
//                                                   COMPONENT CARDS
//------------------------------------------------------------------------------------------------------------------------

export function RenderComponentCards({ Component, Owner, Values, Change, Kit }) {
  const { Card, Control, Metric } = Kit;
  const [Tab, SetTabState] = useState(() => OpenTab.get(Owner?.Id) || "engine");
  const SetTab = (Next) => {
    OpenTab.set(Owner?.Id, Next);
    SetTabState(Next);
  };
  const Settings = ResolveRenderSettings(Values[RenderSettingsKey]);
  const Assign = (Patch) => Change(RenderSettingsKey, { ...Settings, ...Patch });
  const Use = RenderUse(Settings);
  const Tiers = QUALITY_PRESETS.map((Preset) => Preset.label.replace(" (GTX)", "") + (Preset.note ? " 📝" : ""));
  const Slide = (Label, Key, Minimum, Maximum, Decimals = 0, Unit = "") => (
    <Control Field={{ Label, Minimum, Maximum, Decimals, Unit, Default: CanonicalRenderSettings[Key] }} Value={Settings[Key]} Change={(Next) => Assign({ [Key]: Decimals ? Next : Math.round(Next) })} />
  );
  const Switch = (Label, Key) => <Control Field={{ Label, Control: "Switch" }} Value={Settings[Key]} Change={(Next) => Assign({ [Key]: Next })} />;
  const TierPick = (Label, Key, Patch) => <Control Field={{ Label, Control: "Select", Options: Tiers }} Value={Settings[Key]} Change={(Index) => Assign(Patch(Index))} />;
  const Renderer = RENDERERS.find((Each) => Each.id === Tab);
  const Tracers = RENDERERS[2].engines.map((Id) => [Id, Find(RENDER_ENGINES, Id).short]);

  const Body = () => {
    if (Tab === "engine") {
      const Tier = MasterTier(Settings);
      const Budget = SampleBudget(Settings);
      const Active = RendererOf(Settings.RenderEngine);
      const Engine = Find(RENDER_ENGINES, Settings.RenderEngine);
      const Changed = Object.keys(CanonicalRenderSettings).filter((Key) => Settings[Key] !== CanonicalRenderSettings[Key]).length;
      return (
        <>
          <Card Title="Engine" Class="rs-card rc-engine-card">
            <Metric Value={Active.label} Unit={Engine.short} Caption={Engine.desc} />
            <Segments Label="Renderer" Options={RENDERERS.map((Each) => [Each.id, Each.label])} Active={Active.id} Choose={(Id) => Assign(RendererPatch(Id, Settings))} />
            {Active.id === "ray" && <Segments Label="Tracer" Options={Tracers} Active={Settings.RenderEngine} Choose={(Id) => Assign(EnginePatch(Id))} />}
            <dl className="rc-lines">
              {RendererLines(Settings).map(([Label, Text]) => (
                <div key={Label}>
                  <dt>{Label}</dt>
                  <dd>{Text}</dd>
                </div>
              ))}
            </dl>
            <button type="button" className="rc-open" onClick={() => SetTab(Active.id)}>
              {Active.label} settings
              <svg viewBox="0 0 16 16" width="12" height="12" aria-hidden="true">
                <path d="M6 3.5L10.5 8 6 12.5" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" />
              </svg>
            </button>
          </Card>
          <Card Title="Quality" Class="rs-card">
            <Metric Value={Budget.Total.toFixed(Budget.Total < 10 ? 1 : 0)} Unit="samples / pixel" Caption={`${Tier < 0 ? "Custom: the renderers use different tiers" : Tiers[Tier] + " on every renderer"}${Budget.Scale !== 1 ? ` · × ${Budget.Scale.toFixed(2)} for scale` : ""}`} />
            <div className="graphic rs-graphic">
              <BudgetBar Budget={Budget} Tones={Tones} />
            </div>
            <Segments Label="Global quality tier" Options={QUALITY_PRESETS.map((Preset) => [Preset.id, Tiers[Preset.id]])} Active={Tier} Choose={(Index) => Assign(MasterQualityPatch(Index))} />
            <p className="rs-note">A tier sets the shadow, GI and reflection counts of every renderer; each renderer's tab can still change its own.</p>
          </Card>
          <Card Title="Output" Class="rs-card">
            <Metric Value={String(Settings.RenderScale)} Unit="% scale" Caption={`Exposure gain ${Settings.ExposureGain.toFixed(2)} × · edge AA ${Settings.AntiAliasing ? "on" : "off"}`} />
            <div className="graphic rs-graphic">
              <ScaleStudy Settings={Settings} />
            </div>
            {Slide("Exposure gain", "ExposureGain", 0.2, 3.5, 2, "×")}
            <Segments Label="Resolution scale" Options={RENDER_SCALES.map((Each) => [Each, Each + " %"])} Active={Settings.RenderScale} Choose={(Next) => Assign({ RenderScale: Next })} />
            {Switch("Edge anti-aliasing", "AntiAliasing")}
            <small className="rc-caption">Reflection source</small>
            <Segments Label="Reflection source" Options={REFLECTION_SOURCES.map((Each) => [Each.id, Each.pick])} Active={Settings.ReflectionSource} Choose={(Next) => Assign({ ReflectionSource: Next })} />
            <small className="rc-caption">Trace resolution</small>
            <Segments Label="Trace resolution" Options={TRACE_RESOLUTIONS.map((Each) => [Each.id, Each.pick])} Active={Settings.TraceResolution} Choose={(Next) => Assign({ TraceResolution: Next })} />
          </Card>
          <div className="rc-reset">
            <span>{Changed ? `${Changed} of ${Object.keys(CanonicalRenderSettings).length} settings differ from the defaults` : "Source defaults"} · this camera only</span>
            <button type="button" disabled={!Changed} onClick={() => Change(RenderSettingsKey, { ...CanonicalRenderSettings })}>
              Reset
            </button>
          </div>
        </>
      );
    }

    const Strip = <ActiveStrip Renderer={Renderer} Settings={Settings} Assign={Assign} />;

    if (Tab === "pbr") {
      const Shadow = Find(PBR_SHADOWS, Settings.PbrShadow);
      const Pcss = Settings.PbrShadow === "pcss" || Settings.PbrShadow === "contact_pcss";
      const Texel = ((40 / Settings.PbrMapRes) * (Settings.PbrHalfRes ? 2 : 1) * 100).toFixed(1);
      return (
        <>
          {Strip}
          <Card Title="Shadow algorithm" Class="rs-card" Accent={Tones.Shadow}>
            <Metric Value={`${Settings.PbrMapRes}²`} Unit="shadow map" Caption={`${Texel} cm per texel across a 40 m light view · ${Shadow.desc}`} />
            <div className="graphic rs-graphic">
              <KernelStudy Settings={PbrStudy(Settings)} />
            </div>
            <Tiles Label="PBR shadow algorithm" Options={PBR_SHADOWS.map((Option) => ({ ...Option, line: Option.desc.split(".")[0] }))} Active={Settings.PbrShadow} Choose={(Id) => Assign({ PbrShadow: Id })} />
            <Segments Label="Map resolution" Options={[[1024, "1024²"], [2048, "2048²"]]} Active={Settings.PbrMapRes} Choose={(Next) => Assign({ PbrMapRes: Next })} />
            {TierPick("Shadow quality", "PbrShadowQuality", PbrShadowPatch)}
            {Switch("Half-resolution shadows", "PbrHalfRes")}
            {Pcss && Switch("Plane filter", "PbrPlaneFilter")}
          </Card>
          <Card Title="Penumbra" Class="rs-card" Accent={Tones.Shadow}>
            <Metric Value={Settings.PbrSoftness.toFixed(2)} Unit="× softness" Caption={`Area emitters ${Settings.PbrEmitters}`} />
            <div className="graphic rs-graphic">
              <PenumbraStudy Settings={PbrStudy(Settings)} />
            </div>
            {Slide("Penumbra softness", "PbrSoftness", 0.25, 2.5, 2, "×")}
            {Pcss && Slide("PCSS taps", "PbrPcssTaps", 8, 48)}
            {Settings.PbrShadow === "smrt" && Slide("SMRT march steps", "PbrSmrtRays", 2, 12)}
            {Settings.PbrShadow === "contact_pcss" && Slide("Contact trace rays", "PbrContactRays", 1, 16)}
            <Control Field={{ Label: "Area emitters", Control: "Select", Options: ["Shadowed", "Unshadowed"] }} Value={Settings.PbrEmitters === "unshadowed" ? 1 : 0} Change={(Index) => Assign({ PbrEmitters: Index ? "unshadowed" : "shadowed" })} />
          </Card>
          <Card Title="Direct lighting" Class="rs-card">
            {Switch("LTC area lights", "PbrAreaLights")}
            {Slide("Sky & ambient fill", "PbrFill", 0.2, 2.5, 2, "×")}
            <p className="rs-note">OpenPBR Direct has no GI and no SDF: indirect light is the sky and ambient fill only.</p>
          </Card>
          <Card Title="Ambient occlusion" Class="rs-card rc-ao-card" Accent={Tones.Occlusion}>
            <Metric Value={Settings.XeGtao ? String(Settings.XeGtaoSlices) : "Off"} Unit={Settings.XeGtao ? "XeGTAO slices" : ""} Caption={Settings.XeGtao ? `Screen-space horizon search within ${Settings.XeGtaoRadius.toFixed(2)} m · strength ${Settings.XeGtaoStrength.toFixed(2)} ×` : "No ambient occlusion on the PBR raster."} />
            <div className="graphic rs-graphic">
              <OcclusionStudy Method={Settings.XeGtao ? "xegtao" : "off"} Count={Settings.XeGtaoSlices} Radius={Settings.XeGtaoRadius / 3} />
            </div>
            {Switch("XeGTAO", "XeGtao")}
            {Settings.XeGtao && (
              <>
                {Slide("XeGTAO strength", "XeGtaoStrength", 0, 2, 2, "×")}
                {Slide("Effect radius", "XeGtaoRadius", 0.1, 3, 2, "m")}
                {Slide("Slices", "XeGtaoSlices", 1, 9)}
              </>
            )}
          </Card>
        </>
      );
    }

    if (Tab === "sdf") {
      const Mode = Find(SDF_SHADOWS, Settings.SdfShadow);
      return (
        <>
          {Strip}
          <Card Title="Distance-field shadows" Class="rs-card" Accent={Tones.Gi}>
            <Metric Value={String(Mode.ordinal)} Unit={Mode.pick} Caption={`${Mode.desc} · ${Settings.SdfSteps} march steps`} />
            <div className="graphic rs-graphic">
              <SdfTraceStudy Settings={{ ShadowSoftness: Settings.SdfSoftness }} Ordinal={Mode.ordinal} />
            </div>
            <ol className="rc-rail" aria-label="SDF shadow algorithm">
              {SDF_SHADOWS.map((Each) => (
                <li key={Each.id}>
                  <button type="button" aria-pressed={Each.id === Settings.SdfShadow} onClick={() => Assign({ SdfShadow: Each.id })}>
                    <i>{Each.ordinal}</i>
                    <span>
                      <b>{Each.pick}</b>
                      <small>{Each.desc}</small>
                    </span>
                  </button>
                </li>
              ))}
            </ol>
            {Slide("March steps", "SdfSteps", 8, 96)}
            {Slide("Penumbra softness", "SdfSoftness", 0.25, 2.5, 2, "×")}
            {TierPick("Shadow quality", "SdfShadowQuality", SdfShadowPatch)}
          </Card>
          <Card Title="Cone GI" Class="rs-card" Accent={Tones.Gi}>
            <Metric Value={Settings.SdfGi ? String(Settings.SdfCones) : "Off"} Unit={Settings.SdfGi ? "cones" : ""} Caption={Settings.SdfGi ? `Bounce × ${Settings.SdfGiIntensity.toFixed(2)} · ${Tiers[Settings.SdfGiQuality]}` : "Direct light and ambient fill only."} />
            <div className="graphic rs-graphic">
              <HemisphereStudy Settings={SdfGiStudy(Settings)} />
            </div>
            {Switch("SDF cone GI", "SdfGi")}
            {Settings.SdfGi && (
              <>
                {Slide("Cone directions", "SdfCones", 4, 14)}
                {Slide("Bounce intensity", "SdfGiIntensity", 0, 2.5, 2, "×")}
                {TierPick("GI quality", "SdfGiQuality", SdfGiPatch)}
              </>
            )}
            {Slide("Sky & ambient fill", "SdfFill", 0.2, 2.5, 2, "×")}
          </Card>
          <Card Title="SDF ambient occlusion" Class="rs-card rc-ao-card" Accent={Tones.Occlusion}>
            <Metric Value={Settings.SdfAo ? (Settings.SdfAoMethod === "sdf_multitap" ? String(Settings.SdfAoTaps) : Settings.SdfAoMethod === "sdf_cone" ? String(Settings.SdfAoCones) : "3D") : "Off"} Unit={Settings.SdfAo ? (Settings.SdfAoMethod === "sdf_multitap" ? "taps" : Settings.SdfAoMethod === "sdf_cone" ? "cones" : "clipmap") : ""} Caption={Settings.SdfAo ? `${Find(SDF_AO, Settings.SdfAoMethod).desc} Radius ${Settings.SdfAoRadius.toFixed(2)} m.` : "No ambient occlusion on the SDF tracer."} />
            <div className="graphic rs-graphic">
              <OcclusionStudy Method={Settings.SdfAo ? Settings.SdfAoMethod : "off"} Count={Settings.SdfAoMethod === "sdf_multitap" ? Settings.SdfAoTaps : Settings.SdfAoCones} Radius={Settings.SdfAoRadius / 4} />
            </div>
            {Switch("SDF ambient occlusion", "SdfAo")}
            {Settings.SdfAo && (
              <>
                <Tiles Label="SDF AO method" Options={SDF_AO.map((Option) => ({ ...Option, line: Option.desc.split(";")[0] }))} Active={Settings.SdfAoMethod} Choose={(Id) => Assign({ SdfAoMethod: Id })} />
                {Settings.SdfAoMethod === "sdf_multitap" && Slide("Taps", "SdfAoTaps", 3, 12)}
                {Settings.SdfAoMethod === "sdf_cone" && Slide("Cones", "SdfAoCones", 1, 9)}
                {Slide("AO radius", "SdfAoRadius", 0.1, 4, 2, "m")}
                {Slide("AO strength", "SdfAoStrength", 0, 2, 2, "×")}
              </>
            )}
          </Card>
        </>
      );
    }

    if (Tab === "ray") {
      const Shadow = Find(RAY_SHADOWS, Settings.RayShadow);
      const Parts = { Shadow: Settings.RayShadowRays, Gi: Settings.RayGi ? Settings.RayGiSamples * 2 : 0, Reflection: Settings.RayReflections ? Settings.RayReflectionRays * Settings.RayReflectionBounces : 0 };
      return (
        <>
          {Strip}
          <Card Title="Tracer" Class="rs-card" Accent={Tones.Reflection}>
            <Metric Value={String(Parts.Shadow + Parts.Gi + Parts.Reflection)} Unit="rays / pixel" Caption={Find(RENDER_ENGINES, Settings.RayTracer).desc} />
            <div className="graphic rs-graphic">
              <BudgetBar Budget={Parts} Tones={Tones} />
            </div>
            <Segments Label="Tracer" Options={Tracers} Active={Settings.RayTracer} Choose={(Id) => Assign(Use.ray ? EnginePatch(Id) : { RayTracer: Id })} />
          </Card>
          <Card Title="Traced shadows" Class="rs-card" Accent={Tones.Shadow}>
            <Metric Value={String(Settings.RayShadowRays)} Unit="shadow rays" Caption={Shadow.desc} />
            <div className="graphic rs-graphic">
              <PenumbraStudy Settings={RayShadowStudy(Settings)} />
            </div>
            <Tiles Label="Ray-traced shadow algorithm" Options={RAY_SHADOWS.map((Option) => ({ ...Option, line: Option.desc.split(";")[1]?.trim() }))} Active={Settings.RayShadow} Choose={(Id) => Assign({ RayShadow: Id })} />
            {Slide("Shadow rays", "RayShadowRays", 1, 16)}
            {Slide("Penumbra softness", "RaySoftness", 0.25, 2.5, 2, "×")}
            {TierPick("Shadow quality", "RayShadowQuality", RayShadowPatch)}
          </Card>
          <Card Title="Traced GI" Class="rs-card" Accent={Tones.Gi}>
            <div className="graphic rs-graphic">
              <HemisphereStudy Settings={RayGiStudy(Settings)} />
            </div>
            {Switch("Trace GI", "RayGi")}
            {Settings.RayGi && (
              <>
                {Slide("Samples / pixel", "RayGiSamples", 2, 20)}
                {Slide("Bounce intensity", "RayGiIntensity", 0, 2.5, 2, "×")}
                {TierPick("GI quality", "RayGiQuality", RayGiPatch)}
              </>
            )}
            {Slide("Sky & ambient fill", "RayFill", 0.2, 2.5, 2, "×")}
          </Card>
          <Card Title="Traced reflections" Class="rs-card" Accent={Tones.Reflection}>
            <div className="graphic rs-graphic">
              <BounceStudy Settings={RayBounceStudy(Settings)} />
            </div>
            {Switch("Trace reflections", "RayReflections")}
            {Settings.RayReflections ? (
              <>
                {Slide("Rays", "RayReflectionRays", 1, 8)}
                {Slide("Bounces", "RayReflectionBounces", 1, 3)}
                {TierPick("Reflection quality", "RayReflectionQuality", RayReflectionPatch)}
              </>
            ) : (
              <p className="rs-note">Reflections show the sky, clouds, moon and stars only.</p>
            )}
          </Card>
          <Card Title="Traced ambient occlusion" Class="rs-card rc-ao-card" Accent={Tones.Occlusion}>
            <Metric Value={Settings.RayAo ? (Settings.RayAoMethod === "traced_gi" ? "GI" : String(Settings.RayAoRays)) : "Off"} Unit={Settings.RayAo ? (Settings.RayAoMethod === "traced_gi" ? "occlusion" : "AO rays / pixel") : ""} Caption={Settings.RayAo ? `${Find(RAY_AO, Settings.RayAoMethod).desc}${Settings.RayAoMethod === "traced_gi" ? "" : ` Rays reach ${Settings.RayAoRadius.toFixed(2)} m.`}` : "No ambient occlusion on the ray tracer."} />
            <div className="graphic rs-graphic">
              <OcclusionStudy Method={Settings.RayAo ? Settings.RayAoMethod : "off"} Count={Settings.RayAoRays} Radius={Settings.RayAoRadius / 5} />
            </div>
            {Switch("Traced ambient occlusion", "RayAo")}
            {Settings.RayAo && (
              <>
                <Tiles Label="Ray-traced AO method" Options={RAY_AO.map((Option) => ({ ...Option, line: Option.desc.split(",")[0] }))} Active={Settings.RayAoMethod} Choose={(Id) => Assign({ RayAoMethod: Id })} />
                {Settings.RayAoMethod !== "traced_gi" && (
                  <>
                    {Slide("AO rays", "RayAoRays", 1, 16)}
                    {Slide("AO radius", "RayAoRadius", 0.1, 5, 2, "m")}
                  </>
                )}
                {Slide("AO strength", "RayAoStrength", 0, 2, 2, "×")}
              </>
            )}
          </Card>
        </>
      );
    }

    // MatCap.
    const Channel = Find(MATCAP_CHANNELS, Settings.MatcapChannel);
    const Shadow = Find(MATCAP_SHADOWS, Settings.MatcapShadow);
    return (
      <>
        {Strip}
        <Card Title="Studio sphere" Class="rs-card rc-matcap-card">
          <div className="rc-matcap-stage">
            <MatcapBall Settings={Settings} SetLight={(Next) => Assign({ MatcapLight: Next })} />
            <small>drag around the sphere to move the light</small>
          </div>
          <Segments Label="Channel" Options={MATCAP_CHANNELS.map((Option) => [Option.id, Option.label])} Active={Settings.MatcapChannel} Choose={(Id) => Assign({ MatcapChannel: Id })} />
          <p className="rs-note">{Channel.desc}</p>
        </Card>
        <Card Title="Studio shadow" Class="rs-card" Accent={Tones.Shadow}>
          <Metric Value={Settings.MatcapShadow === "none" ? "Off" : String(Math.round(Settings.MatcapShadowStrength * 100))} Unit={Settings.MatcapShadow === "none" ? "" : "% strength"} Caption={`${Shadow.pick}: ${Shadow.desc.charAt(0).toLowerCase() + Shadow.desc.slice(1)}`} />
          <div className="graphic rs-graphic">
            <StudioShadowStudy Mode={Settings.MatcapShadow} Strength={Settings.MatcapShadowStrength} Softness={Settings.MatcapShadowSoftness} Light={Settings.MatcapLight} />
          </div>
          <Tiles Label="MatCap shadow algorithm" Options={MATCAP_SHADOWS.map((Option) => ({ ...Option, line: Option.desc }))} Active={Settings.MatcapShadow} Choose={(Id) => Assign({ MatcapShadow: Id })} />
          {Settings.MatcapShadow !== "none" && (
            <>
              {Slide("Shadow strength", "MatcapShadowStrength", 0, 1, 2)}
              {Settings.MatcapShadow !== "cavity" && Slide("Shadow softness", "MatcapShadowSoftness", 0.25, 2.5, 2, "×")}
            </>
          )}
        </Card>
        <Card Title="Studio light" Class="rs-card">
          {Slide("Light angle", "MatcapLight", 0, 360, 0, "°")}
          {Slide("Rim", "MatcapRim", 0, 1, 2)}
          {Slide("Cavity", "MatcapCavity", 0, 1, 2)}
          <Control Field={{ Label: "Clay tint", Control: "Colour" }} Value={Settings.MatcapTint} Change={(Next) => Assign({ MatcapTint: Next })} />
          {Switch("Wire overlay", "MatcapWire")}
          <p className="rs-note">No GI and no SDF: MatCap reads shape only.</p>
        </Card>
      </>
    );
  };

  return (
    <div className="rc-cards" data-render-component={Component} data-render-tab={Tab}>
      <div className="rc-tabs" role="tablist" aria-label="Render tabs">
        {Tabs.map(([Id, Label]) => (
          <button key={Id} type="button" role="tab" aria-selected={Tab === Id} data-tab={Id} onClick={() => SetTab(Id)}>
            {Id !== "engine" && <i className={Use[Id] ? "on" : ""} aria-label={Use[Id] ? "rendering" : undefined} />}
            {Label}
          </button>
        ))}
      </div>
      {Body()}
    </div>
  );
}
