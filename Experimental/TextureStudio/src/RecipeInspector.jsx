import { DesignerControlQuantity, ProjectDesignerControl, ReadMaterialParameter, WriteMaterialParameter } from "./MaterialSpecification.js";
import React, { useState } from "react";
import {
  ChevronDown,
  Layers3,
  Sparkles,
  SlidersHorizontal,
  ShieldCheck,
} from "lucide-react";
import { Slider, ColorField, ColorRamp } from "./MaterialControls";
import SandLayersEditor from "./SandLayersEditor.jsx";
import {
  getRecipe,
  recipeControlValue,
  applyRecipeControl,
  applyRecipeColor,
  basicWeaves,
} from "./materialProfiles";

export default function RecipeInspector({ params: p, update, setParams }) {
  const recipe = getRecipe(p);
  const [closed, setClosed] = useState({
    construction: false,
    detail: true,
    colors: true,
  });
  const section = (id, title, children, tag) => (
    <section className="inspector-section recipe-section" key={id}>
      <button
        className="section-title"
        aria-expanded={!closed[id]}
        onClick={() => setClosed((v) => ({ ...v, [id]: !v[id] }))}
      >
        <span>
          {id === "iridescence" || id === "flakes" ? (
            <Sparkles size={13} />
          ) : (
            <Layers3 size={13} />
          )}{" "}
          {title}
        </span>
        <span>
          {tag && <small>{tag}</small>}
          <ChevronDown size={13} className={closed[id] ? "collapsed" : ""} />
        </span>
      </button>
      {!closed[id] && <div className="section-content">{children}</div>}
    </section>
  );
  const control = (c) => (
    <div className="recipe-control" key={c.id}>
      <Slider
        label={c.label}
        value={p.Authoring ? DesignerControlQuantity(p, c) : recipeControlValue(p, c)}
        onChange={(v) => setParams((q) => q.Authoring ? ProjectDesignerControl(q, c, v) : applyRecipeControl(q, c, v))}
        min={c.direct ? c.min : 0}
        max={c.direct ? c.max : 1}
        step={c.direct ? c.step : 0.01}
        log={!!c.log}
        extend={!!c.extend}
        unit={c.unit || ""}
        percentage={!c.direct || c.id === "flakes"}
      />
      {c.hint && <p className="help-text">{c.hint}</p>}
    </div>
  );
  const group = (id) =>
    recipe.controls.filter((c) => c.group === id).map(control);
  const weaves = basicWeaves(),
    weave = weaves.find((w) => w.id === p.weavePattern) || weaves[1];
  return (
    <>
      <div className="recipe-intro">
        <div>
          <span className="recipe-eyebrow">
            <ShieldCheck size={11} /> DESIGNER CONTROLS
          </span>
          <span className="recipe-chip">TUNED</span>
        </div>
        <h4>{recipe.title}</h4>
        <p>{recipe.caption}</p>
      </div>
      {section(
        "finish",
        recipe.woven ? "Yarn color & feel" : "Color & finish",
        <>
          {recipe.colors.map((c) => (
            <ColorField
              key={c.key}
              label={c.label}
              value={p.Authoring ? ReadMaterialParameter(p, c.key) : p[c.key]}
              onChange={(v) => setParams((q) => q.Authoring ? WriteMaterialParameter(q, c.key, v) : applyRecipeColor(q, c.key, v))}
            />
          ))}
          {group("finish")}
          <div className="recipe-range-note">
            <ShieldCheck size={12} />
            <span>
              Material-specific ranges. Physical values stay in balance.
            </span>
          </div>
        </>,
      )}
      {recipe.supportsScratches &&
        section(
          "scratches",
          recipe.supportsMetalScratches ? "Metal scratches" : "Scratches",
          <>
            <label className="scratch-switch">
              <span>Enable scratches</span>
              <input
                type="checkbox"
                role="switch"
                aria-label="Enable scratches"
                checked={p.metalScratches === true}
                onChange={(e) => update("metalScratches", e.target.checked)}
              />
            </label>
            <p className="help-text">
              Optional surface cuts. The base finish stays unchanged when off.
              Direction is independent of other surface grain directions.
            </p>
            {p.metalScratches === true && group("scratches")}
          </>,
          p.metalScratches ? "ON" : "OFF",
        )}
      {recipe.iridescent &&
        section(
          "iridescence",
          "Iridescent finish",
          <>
            {group("iridescence")}
            <div className="film-spectrum" />
            <p className="help-text">
              Orbit the surface to see the shift. Thin-film interference changes
              the reflection color with viewing angle—not just a painted-on
              gradient.
            </p>
          </>,
          "THIN FILM",
        )}
      {recipe.woven &&
        section(
          "construction",
          "Weave construction",
          <>
            <label className="weave-select">
              <span>Construction</span>
              <select
                aria-label="Weave construction"
                value={p.weavePattern}
                onChange={(e) => update("weavePattern", e.target.value)}
              >
                {weaves.map((w) => (
                  <option key={w.id} value={w.id}>
                    {w.name}
                  </option>
                ))}
              </select>
              <ChevronDown size={13} />
            </label>
            <p className="weave-description">{weave.description}</p>
            {group("construction")}
            <p className="help-text">
              Yarn colors follow the over/under pattern. On the frozen cloth,
              the weave follows every fold.
            </p>
          </>,
          "9 WEAVES",
        )}
      {p.type === 21 &&
        p.sheetFinish > 0 &&
        section(
          "construction",
          "Sheet corrugation",
          <>
            <label className="weave-select">
              <span>Profile</span>
              <select
                aria-label="Corrugation profile"
                value={p.ribProfile}
                onChange={(e) => update("ribProfile", Number(e.target.value))}
              >
                <option value="0">Rounded corrugation</option>
                <option value="1">Box / IBR ribs</option>
                <option value="2">Roller-door ribs</option>
              </select>
            </label>
            {group("construction")}
            <p className="help-text">
              Rib relief changes shading and exported normals/height, not sheet
              thickness, silhouette or structural geometry.
            </p>
          </>,
        )}
      {p.type === 37 &&
        section(
          "grains",
          "Sand grains",
          <>
            {group("grains")}
            <p className="help-text">
              Sizes are nominal mineral-grain diameters at repetition 1 (100
              scene units per metre). Grain identity seeds size, roughness,
              specular and facet direction independently. Use Macro to see
              individual grains; unresolved grains are filtered.
            </p>
          </>,
          "DIELECTRIC",
        )}
      {p.type === 38 &&
        section(
          "sandBeds",
          "Sand layer beds",
          <SandLayersEditor params={p} setParams={setParams} />,
          `${p.sandLayers?.length || 1} / 4`,
        )}
      {p.type === 38 &&
        p.sandArrangement === "bands" &&
        section("strata", "Sediment bands", <>{group("strata")}</>)}
      {p.type === 0 &&
        section(
          "flakes",
          "Flake character",
          <>
            {group("flakes")}
            <p className="help-text">
              Sparkle tunes micro-roughness and orientation; reflectivity
              independently bounds the metallic response. Cellular particles
              remain layered beneath the coat.
            </p>
          </>,
          "CELLULAR",
        )}
      {p.type === 0 &&
        section(
          "colors",
          "Flake colors",
          <ColorRamp params={p} update={update} setParams={setParams} />,
        )}
      {group("detail").length > 0 &&
        section(
          "detail",
          recipe.woven ? "Thread detail" : "Surface detail",
          <>
            {group("detail")}
            {recipe.id === "wornPlastic" && (
              <p className="help-text">
                Abrasion flattens the raised grain. The recessed surface keeps
                its texture.
              </p>
            )}
          </>,
        )}
      <div className="recipe-footer">
        <SlidersHorizontal size={16} />
        <div>
          <strong>Fewer knobs. Better materials.</strong>
          <p>
            Designer labels and ranges project into the same PBR channels and generator inputs. No separate material values are stored here.
          </p>
        </div>
      </div>
    </>
  );
}
