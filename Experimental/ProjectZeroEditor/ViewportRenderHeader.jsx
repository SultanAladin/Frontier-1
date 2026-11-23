//============================================================================================================================================
//                                                         VIEWPORTRENDERHEADER.JSX
//============================================================================================================================================
// 🧩 Render settings and viewport toggles in the viewport settings sheet (Project Zero specification §10, Follow-up 48). Engine,
// tier, shadows, penumbra, reflections, trace resolution and scale edit the Editor Camera's Render Engine component; the toggles edit
// the editor settings and scene visibility. Browser authoring only: no renderer is driven.

import React from "react";
import {
  RENDER_ENGINES,
  PBR_SHADOWS,
  SDF_SHADOWS,
  QUALITY_PRESETS,
  RENDER_SCALES,
  REFLECTION_SOURCES,
  TRACE_RESOLUTIONS,
  RendererOf,
  EnginePatch,
  MasterQualityPatch,
  MasterTier,
  OcclusionOf,
} from "./RenderSettings.js";

// The specification's five shadow algorithms: four shadow-map filters (PBR raster) and WideTree ray queries (ray tracing).
export const ShadowAlgorithms = [
  ...PBR_SHADOWS.map((Option) => ({ id: Option.id, label: Option.pick })),
  { id: "widetree", label: "WideTree RayQuery" },
];

function Pick({ Label, Options, Active, Choose, Note }) {
  return (
    <label className="vrh-field">
      <span>
        {Label}
        {Note && <span className="specification-note" title={Note}>📝</span>}
      </span>
      <select aria-label={Label} value={Active} onChange={(Event) => Choose(Event.target.value)}>
        {Options.map(([Id, Text]) => (
          <option key={Id} value={Id}>
            {Text}
          </option>
        ))}
      </select>
    </label>
  );
}

function Toggle({ Label, On, Change }) {
  return (
    <label className="viewport-setting-toggle vrh-toggle">
      {Label}
      <input type="checkbox" aria-label={Label} checked={!!On} onChange={(Event) => Change(Event.target.checked)} />
    </label>
  );
}

export default function ViewportRenderHeader({ Render, AssignRender, Settings, AssignSetting, Hidden, SetVisible, CloudShadows, SetCloudShadows, SplitView, SetSplitView }) {
  const Renderer = RendererOf(Render.RenderEngine).id;
  const Tier = MasterTier(Render);
  const Occlusion = OcclusionOf(Render);
  const Shadow = Renderer === "ray" ? "widetree" : Render.PbrShadow;
  const ChooseShadow = (Id) => {
    if (Id === "widetree") AssignRender({ RayShadow: "bvh_disk", ...EnginePatch(Render.RayTracer) });
    else AssignRender({ PbrShadow: Id, ...(Renderer === "ray" ? EnginePatch("openpbr_direct") : {}) });
  };
  return (
    <div className="viewport-render-header" aria-label="Render settings">
      <h4>Render</h4>
      <Pick
        Label="Render engine"
        Options={RENDER_ENGINES.map((Engine) => [Engine.id, Engine.label])}
        Active={Render.RenderEngine}
        Choose={(Id) => AssignRender(EnginePatch(Id))}
      />
      <Pick
        Label="Quality tier"
        Options={[...(Tier < 0 ? [["-1", "Custom (renderers differ)"]] : []), ...QUALITY_PRESETS.map((Preset) => [String(Preset.id), Preset.label.replace(" (GTX)", "") + (Preset.note ? " 📝" : "")])]}
        Active={String(Tier)}
        Choose={(Id) => +Id >= 0 && AssignRender(MasterQualityPatch(+Id))}
      />
      <Pick Label="Shadow algorithm" Options={ShadowAlgorithms.map((Option) => [Option.id, Option.label])} Active={Shadow} Choose={ChooseShadow} />
      <Pick
        Label="SDF penumbra mode"
        Options={SDF_SHADOWS.map((Option) => [Option.id, Option.pick])}
        Active={Render.SdfShadow}
        Choose={(Id) => AssignRender({ SdfShadow: Id })}
      />
      <Pick
        Label="Reflection source"
        Options={REFLECTION_SOURCES.map((Option) => [Option.id, Option.pick])}
        Active={Render.ReflectionSource}
        Choose={(Id) => AssignRender({ ReflectionSource: Id })}
      />
      <Pick
        Label="Trace resolution"
        Options={TRACE_RESOLUTIONS.map((Option) => [String(Option.id), Option.pick])}
        Active={String(Render.TraceResolution)}
        Choose={(Id) => AssignRender({ TraceResolution: +Id })}
      />
      <Pick
        Label="Render scale"
        Options={RENDER_SCALES.map((Scale) => [String(Scale), Scale + " %"])}
        Active={String(Render.RenderScale)}
        Choose={(Id) => AssignRender({ RenderScale: +Id })}
      />
      <h4>Toggles</h4>
      <div className="vrh-toggles">
        <Toggle Label="Reflections" On={Settings.Reflection > 0} Change={(On) => AssignSetting("Reflection", On ? 2 : 0)} />
        <Toggle Label="Anti-aliasing" On={Render.AntiAliasing} Change={(On) => { AssignRender({ AntiAliasing: On }); AssignSetting("AA", On); }} />
        <Toggle Label="Raytracing" On={Settings.Raytracing} Change={(On) => AssignSetting("Raytracing", On)} />
        {/* Follow-up 46: the active renderer's own ambient occlusion (XeGTAO on PBR, SDF AO, traced AO); MatCap has none. */}
        {Occlusion && <Toggle Label={Occlusion.Renderer === "pbr" ? "XeGTAO" : "Ambient occlusion"} On={Render[Occlusion.Switch]} Change={(On) => AssignRender({ [Occlusion.Switch]: On })} />}
        <Toggle Label="Atmosphere" On={!Hidden.atmosphere} Change={(On) => SetVisible("atmosphere", On)} />
        <Toggle Label="Clouds" On={!Hidden.clouds} Change={(On) => SetVisible("clouds", On)} />
        <Toggle Label="Cloud shadows" On={CloudShadows} Change={SetCloudShadows} />
        <Toggle Label="Stars" On={!Hidden.stars} Change={(On) => SetVisible("stars", On)} />
        <Toggle Label="Split viewport" On={SplitView} Change={SetSplitView} />
      </div>
      {Occlusion && (
        <label className="vrh-field vrh-slider" title={Occlusion.Line}>
          <span>{Occlusion.Renderer === "pbr" ? "XeGTAO strength" : `AO strength · ${Occlusion.Label}`}</span>
          <input
            type="range"
            aria-label={Occlusion.Renderer === "pbr" ? "XeGTAO strength" : "AO strength"}
            min="0"
            max="2"
            step="0.05"
            value={Render[Occlusion.Strength]}
            disabled={!Render[Occlusion.Switch]}
            onChange={(Event) => AssignRender({ [Occlusion.Strength]: +Event.target.value })}
          />
          <output>{Render[Occlusion.Strength].toFixed(2)} ×</output>
        </label>
      )}
    </div>
  );
}
