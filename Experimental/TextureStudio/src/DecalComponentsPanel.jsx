import { texturePart } from "./textureSurfaceModel.js";
import { uvTile } from "./textureUVGeometry.js";
import AuthoringNameField from "./AuthoringNameField.jsx";
import React, { useEffect, useState } from "react";
import {
  Stamp,
  Plus,
  Move3D,
  MousePointer2,
  Eye,
  EyeOff,
  Copy,
  Trash2,
  LockKeyhole,
  UnlockKeyhole,
  ArrowUpRight,
} from "lucide-react";
import { Slider } from "./MaterialControls.jsx";
import { StampChannelControls } from "./StampStudio.jsx";
import "./decalStudio.css";
export default function DecalComponentsPanel({ layer, doc, context, locked }) {
  const [open, setOpen] = useState(
    layer.kind === "decal" || layer.decals.length > 0,
  );
  useEffect(
    () => setOpen(layer.kind === "decal" || layer.decals.length > 0),
    [layer.id, context.id],
  );
  const component = layer.decals.find((d) => d.id === context.id);
  const stamp = component
    ? doc.stamps.find((s) => s.id === component.stampId)
    : context.preset;
  const readOnly = locked || component?.locked || context.partLocked;
  const isUV = component?.space === "uv",
    brushUV = context.space === "uv",
    part = texturePart(component?.target?.partId);
  const patch = (changes, key) => context.onPatch(changes, key);
  return (
    <section
      className="tp-property-card tp-decals-card"
      aria-label="Layer decal components"
    >
      <h2>
        <Stamp size={14} /> Decal components{" "}
        <small>{layer.decals.length}</small>
        <button
          aria-label={
            open ? "Collapse decal components" : "Expand decal components"
          }
          onClick={() => setOpen(!open)}
        >
          {open ? "−" : "+"}
        </button>
      </h2>
      {!open && (
        <button
          className="tp-add-decal"
          disabled={locked || !context.preset || !context.placementAvailable}
          onClick={() => {
            setOpen(true);
            context.onArm("brush");
          }}
        >
          <Plus size={14} /> Add decal component
        </button>
      )}
      {(open || context.active) && (
        <>
          <label className="tp-field-row">
            <span>Stamp preset</span>
            <select
              aria-label="Decal stamp preset"
              value={context.preset?.id || ""}
              disabled={locked}
              onChange={(e) => context.onPreset(e.target.value)}
            >
              {!context.presets.length && (
                <option value="">Preparing maker mark…</option>
              )}
              {context.presets.map((p) => (
                <option key={p.id} value={p.id}>
                  {p.name}
                </option>
              ))}
            </select>
          </label>
          {context.preset && (
            <div className="tp-decal-preset">
              <img
                src={context.preset.raster.dataUrl}
                alt={`${context.preset.name} stamp preview`}
              />
              <div>
                <strong>{context.preset.name}</strong>
                <small>
                  {context.preset.project.layers.length} artwork components ·{" "}
                  {context.preset.project.channels.length} targets
                </small>
                <button
                  onClick={() => context.onEditStamp(context.preset.project)}
                >
                  <ArrowUpRight size={11} /> Edit in Stamp
                </button>
              </div>
            </div>
          )}
          <div className="tp-decal-placement-buttons">
            <button
              aria-label="Stamp decal brush"
              aria-pressed={context.active && context.mode === "brush"}
              disabled={
                locked || !context.preset || !context.placementAvailable
              }
              onClick={() => context.onArm("brush")}
            >
              <Stamp size={15} /> Stamp brush
            </button>
            <button
              aria-label="Transform decal placement"
              aria-pressed={context.active && context.mode === "transform"}
              disabled={readOnly || !context.preset}
              onClick={() => context.onArm(component ? "transform" : "place")}
            >
              <Move3D size={15} /> Transform
            </button>
          </div>
          <label className="tp-toggle-row">
            <span>New stamps as masks</span>
            <input
              type="checkbox"
              aria-label="Stamp brush as mask"
              disabled={locked}
              checked={context.brush.asMask}
              onChange={(e) => context.onBrush({ asMask: e.target.checked })}
            />
          </label>
          <Slider
            label={brushUV ? "Stamp size · UV" : "Stamp size"}
            ariaLabel="Decal brush size"
            min={brushUV ? 0.001 : 0.05}
            max={2}
            step={brushUV ? 0.001 : 0.01}
            unit={brushUV ? "UV" : ""}
            disabled={locked}
            value={context.brush.size[0]}
            onChange={(size) => context.onBrush({ size: [size, size] })}
          />
          <Slider
            label="New stamp opacity"
            ariaLabel="Decal brush opacity"
            percentage
            disabled={locked}
            value={context.brush.opacity}
            onChange={(opacity) => context.onBrush({ opacity })}
          />
          <Slider
            label="New stamp rotation"
            ariaLabel="Decal brush rotation"
            min={-180}
            max={180}
            step={1}
            unit="°"
            disabled={locked}
            value={context.brush.roll}
            onChange={(roll) => context.onBrush({ roll })}
          />
          <p className="tp-card-note">
            {brushUV
              ? "Stamp onto a UV island in Authoring UDIMs. UV decals map onto the real 3D surface; move, rotate or scale by dragging artwork in that view."
              : context.active
                ? context.mode === "brush"
                  ? "Aim at the surface and click once per stamp. Dragging does not spray or orbit; choose Navigate to orbit."
                  : context.mode === "place"
                    ? "Click the surface once to place a decal, then use its 3D transform handles."
                    : "Drag the 3D handles to move, rotate or scale. Re-anchor snaps the projector to a new surface hit."
                : "Stamp onto the teapot, or create a surface placement and adjust it with transform handles."}
          </p>
          <div
            className="tp-decal-component-list"
            role="list"
            aria-label="Decal components"
          >
            {[...layer.decals].reverse().map((decal) => {
              const source = doc.stamps.find((s) => s.id === decal.stampId);
              return (
                <div
                  key={decal.id}
                  className={`tp-decal-component ${context.id === decal.id ? "selected" : ""}`}
                  role="listitem"
                >
                  <button
                    aria-label={`Inspect decal ${decal.name}`}
                    onClick={() => context.onSelect(decal.id)}
                  >
                    <img src={source?.raster.dataUrl} alt="" />
                    <span>
                      {decal.name}
                      <small>
                        {decal.asMask
                          ? "Surface mask"
                          : `${decal.channels.length} channels`}{" "}
                        · {Math.round(decal.opacity * 100)}% ·{" "}
                        {decal.space === "uv" ? "UV" : "3D"}
                        {decal.target
                          ? ` · ${texturePart(decal.target.partId).name}`
                          : ""}
                      </small>
                    </span>
                  </button>
                  <button
                    aria-label={`${decal.visible ? "Hide" : "Show"} decal ${decal.name}`}
                    disabled={locked}
                    onClick={() =>
                      context.onPatchId(decal.id, { visible: !decal.visible })
                    }
                  >
                    {decal.visible ? <Eye size={12} /> : <EyeOff size={12} />}
                  </button>
                </div>
              );
            })}
          </div>
          <button
            className="tp-add-decal"
            disabled={locked || !context.preset || !context.placementAvailable}
            onClick={() => context.onArm("brush")}
          >
            <Plus size={13} /> Add decal component{" "}
            <span>{brushUV ? "Click UV island" : "Click surface"}</span>
          </button>
          {component && (
            <div className="tp-decal-properties">
              <div className="tp-decal-identity">
                <Stamp size={16} />
                <strong>
                  {component.asMask
                    ? "Stamp mask"
                    : isUV
                      ? "UV decal"
                      : "Surface decal"}
                </strong>
                <button
                  aria-label={
                    component.locked
                      ? "Unlock decal component"
                      : "Lock decal component"
                  }
                  disabled={locked}
                  onClick={() => patch({ locked: !component.locked })}
                >
                  {component.locked ? (
                    <LockKeyhole size={13} />
                  ) : (
                    <UnlockKeyhole size={13} />
                  )}
                </button>
              </div>
              <p className="tp-card-note">
                {isUV
                  ? `Authoring UV coordinates · UDIM ${uvTile(...component.uvPosition)?.id}`
                  : "Object-space projector"}
                {part ? ` · ${part.name} only` : ""}
              </p>
              <label className="tp-field-row">
                <span>Name</span>
                <AuthoringNameField
                  ariaLabel="Decal component name"
                  value={component.name}
                  disabled={readOnly}
                  onChange={(name) => patch({ name }, "name")}
                />
              </label>
              <label className="tp-toggle-row">
                <span>Use as layer mask</span>
                <input
                  type="checkbox"
                  aria-label="Use decal as mask"
                  checked={component.asMask}
                  disabled={readOnly}
                  onChange={(e) => patch({ asMask: e.target.checked })}
                />
              </label>
              {component.asMask && (
                <label className="tp-toggle-row">
                  <span>Invert stamp mask</span>
                  <input
                    type="checkbox"
                    aria-label="Invert decal mask"
                    checked={component.inverted}
                    disabled={readOnly}
                    onChange={(e) => patch({ inverted: e.target.checked })}
                  />
                </label>
              )}
              <div className="tp-decal-gizmo-modes">
                {["translate", "rotate", "scale"].map((mode) => (
                  <button
                    key={mode}
                    aria-label={`Decal ${mode} gizmo`}
                    disabled={readOnly}
                    aria-pressed={context.gizmo === mode}
                    onClick={() => context.onGizmo(mode)}
                  >
                    {mode === "translate"
                      ? "Move"
                      : mode === "rotate"
                        ? "Rotate"
                        : "Scale"}
                  </button>
                ))}
              </div>
              <button
                className="tp-decal-reanchor"
                disabled={readOnly}
                onClick={() => context.onArm("place")}
              >
                <MousePointer2 size={12} />{" "}
                {isUV ? "Re-anchor in UV space" : "Re-anchor to surface"}
              </button>
              <div className="tp-shared-controls">
                <Slider
                  label="Width"
                  ariaLabel="Decal width"
                  min={isUV ? 0.001 : 0.01}
                  max={4}
                  step={isUV ? 0.001 : 0.01}
                  unit={isUV ? "UV" : ""}
                  disabled={readOnly}
                  value={component.size[0]}
                  onChange={(width) =>
                    patch({ size: [width, component.size[1]] }, "width")
                  }
                />
                <Slider
                  label="Height"
                  ariaLabel="Decal height"
                  min={isUV ? 0.001 : 0.01}
                  max={4}
                  step={isUV ? 0.001 : 0.01}
                  unit={isUV ? "UV" : ""}
                  disabled={readOnly}
                  value={component.size[1]}
                  onChange={(height) =>
                    patch({ size: [component.size[0], height] }, "height")
                  }
                />
                {!isUV && (
                  <Slider
                    label="Projection depth"
                    ariaLabel="Decal projection depth"
                    min={0.01}
                    max={2}
                    step={0.01}
                    disabled={readOnly}
                    value={component.depth}
                    onChange={(depth) => patch({ depth }, "depth")}
                  />
                )}
                <Slider
                  label="Rotation"
                  ariaLabel="Decal rotation"
                  min={-180}
                  max={180}
                  step={1}
                  unit="°"
                  disabled={readOnly}
                  value={component.roll}
                  onChange={(roll) => patch({ roll }, "roll")}
                />
                <Slider
                  label="Opacity / mask strength"
                  ariaLabel="Decal opacity"
                  percentage
                  disabled={readOnly}
                  value={component.opacity}
                  onChange={(opacity) => patch({ opacity }, "opacity")}
                />
                {isUV &&
                  ["U", "V"].map((axis, i) => (
                    <Slider
                      key={axis}
                      label={`Position ${axis}`}
                      ariaLabel={`Decal UV ${axis}`}
                      min={i === 0 ? part?.u || 0 : 0}
                      max={
                        i === 0 ? (part ? part.u + 0.99999 : 9.99999) : 0.99999
                      }
                      step={0.001}
                      unit="UV"
                      disabled={readOnly}
                      value={component.uvPosition[i]}
                      onChange={(value) =>
                        patch(
                          {
                            uvPosition: component.uvPosition.map((n, j) =>
                              j === i ? value : n,
                            ),
                          },
                          `uv-${axis}`,
                        )
                      }
                    />
                  ))}
                {!isUV &&
                  ["X", "Y", "Z"].map((axis, index) => (
                    <Slider
                      key={axis}
                      label={`Position ${axis}`}
                      ariaLabel={`Decal position ${axis}`}
                      min={-4}
                      max={4}
                      step={0.01}
                      disabled={readOnly}
                      value={component.position[index]}
                      onChange={(value) =>
                        patch(
                          {
                            position: component.position.map((v, i) =>
                              i === index ? value : v,
                            ),
                          },
                          `position-${axis}`,
                        )
                      }
                    />
                  ))}
              </div>
              {!component.asMask && (
                <StampChannelControls
                  prefix="Decal"
                  disabled={readOnly}
                  channels={component.channels}
                  values={component.channelValues}
                  onChange={(changes) =>
                    patch(changes, changes.channelValues ? "channels" : null)
                  }
                />
              )}
              {component.asMask && (
                <p className="tp-card-note">
                  {stamp?.project.maskMode === "luminance"
                    ? "Luminance × alpha"
                    : "Alpha silhouette"}{" "}
                  projected onto this layer's selected fill / gradients and
                  decal components. Multiple mask stamps combine as a union;
                  ancestor coverage multiplies. No mesh UV atlas is required.
                </p>
              )}
              <button
                className="tp-decal-replace"
                disabled={readOnly || !context.preset}
                onClick={() => context.onReplace(component.id)}
              >
                Replace artwork & targets with selected preset
              </button>
              <div className="tp-decal-bottom-actions">
                <button
                  disabled={readOnly}
                  onClick={() => context.onDuplicate(component.id)}
                >
                  <Copy size={13} /> Duplicate
                </button>
                <button
                  disabled={readOnly}
                  onClick={() => context.onRemove(component.id)}
                >
                  <Trash2 size={13} /> Remove
                </button>
                <button onClick={() => context.onEditStamp(stamp.project)}>
                  <ArrowUpRight size={13} /> Edit artwork
                </button>
              </div>
            </div>
          )}
        </>
      )}
    </section>
  );
}
