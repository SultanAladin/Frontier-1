//==========================================================================================================================================
//                                                    WORLDDRAGDROP.JS
//==========================================================================================================================================
// Unreal Engine-style drag-and-drop coordinator + automotive safety cone presets ("Traffic Cone" & "Cylinder Cone")
// for live 3D raycast viewport placement from Asset Browser, Construct, and Quick-Place Shelf.

import { ChooseMaterialType, ResolveMaterial } from "./MaterialSpecification.js";
import { GetWorldPropById, WORLD_PROP_CATALOG } from "./WorldPropLibrary.js";

function DefaultMaterial(Type = "Standard") {
  return ChooseMaterialType(ResolveMaterial({}), Type);
}

export const WORLD_ACTOR_DRAG_MIME = "application/x-frontier-actor";

let ActiveWorldDrag = null;
const WorldDragListeners = new Set();

function NotifyDragListeners() {
  for (const Listener of WorldDragListeners) {
    try {
      Listener(ActiveWorldDrag);
    } catch {
      /* Listener errors never interrupt drag sessions */
    }
  }
}

export function SubscribeWorldDrag(Listener) {
  WorldDragListeners.add(Listener);
  return () => WorldDragListeners.delete(Listener);
}

export function GetActiveWorldDrag() {
  return ActiveWorldDrag;
}

export function BeginWorldActorDrag(Event, Payload) {
  if (!Payload) return;
  ActiveWorldDrag = Payload;
  if (Event?.dataTransfer) {
    try {
      const Serialized = JSON.stringify(Payload);
      Event.dataTransfer.setData(WORLD_ACTOR_DRAG_MIME, Serialized);
      Event.dataTransfer.setData("text/plain", Payload.Label || Payload.Template?.Name || "Actor");
      Event.dataTransfer.effectAllowed = "copy";
    } catch {
      /* Synthetic or restricted DataTransfer */
    }
  }
  if (typeof document !== "undefined") {
    document.body.classList.add("dragging-world-actor");
  }
  NotifyDragListeners();
}

export function EndWorldActorDrag() {
  if (!ActiveWorldDrag && typeof document !== "undefined" && !document.body.classList.contains("dragging-world-actor")) {
    return;
  }
  ActiveWorldDrag = null;
  if (typeof document !== "undefined") {
    document.body.classList.remove("dragging-world-actor");
  }
  NotifyDragListeners();
}

export function ReadWorldActorDropData(Event) {
  if (Event?.dataTransfer) {
    try {
      const Raw = Event.dataTransfer.getData(WORLD_ACTOR_DRAG_MIME);
      if (Raw) return JSON.parse(Raw);
    } catch {
      /* Fall back to in-memory drag payload */
    }
  }
  return ActiveWorldDrag;
}

// Tapered street / track pylon ("cone-like cone" for cars) — square matte rubber base + vivid safety orange + retro-reflective white collar
export function BuildTrafficConeMaterial() {
  const Material = DefaultMaterial("Standard");
  Material.Channels.colour = {
    Source: "Gradient",
    Fill: "#ff5414",
    Axis: "V",
    Repeat: 1,
    Component: "RGB",
    Stops: [
      { Position: 0.0, Value: "#1d2025" },
      { Position: 0.11, Value: "#1d2025" },
      { Position: 0.125, Value: "#ff5414" },
      { Position: 0.52, Value: "#ff5414" },
      { Position: 0.535, Value: "#f4f6fb" },
      { Position: 0.725, Value: "#f4f6fb" },
      { Position: 0.74, Value: "#ff5414" },
      { Position: 1.0, Value: "#ff5414" },
    ],
  };
  Material.Channels.roughness = {
    Source: "Gradient",
    Fill: 0.34,
    Axis: "V",
    Repeat: 1,
    Component: "R",
    Stops: [
      { Position: 0.0, Value: 0.78 },
      { Position: 0.11, Value: 0.78 },
      { Position: 0.13, Value: 0.34 },
      { Position: 0.52, Value: 0.34 },
      { Position: 0.54, Value: 0.14 },
      { Position: 0.72, Value: 0.14 },
      { Position: 0.74, Value: 0.34 },
      { Position: 1.0, Value: 0.34 },
    ],
  };
  Material.Channels.coat = { Source: "Fill", Fill: 0.28, Axis: "U", Repeat: 1, Component: "R", Stops: [] };
  Material.Channels.coatRoughness = { Source: "Fill", Fill: 0.18, Axis: "U", Repeat: 1, Component: "R", Stops: [] };
  return Material;
}

// Steep cylinder-like car lane delineator / bollard cone ("cylinder-like cone" for cars) — round rubber base + dual reflective collars
export function BuildCylinderConeMaterial() {
  const Material = DefaultMaterial("Standard");
  Material.Channels.colour = {
    Source: "Gradient",
    Fill: "#f03e18",
    Axis: "V",
    Repeat: 1,
    Component: "RGB",
    Stops: [
      { Position: 0.0, Value: "#1a1d22" },
      { Position: 0.09, Value: "#1a1d22" },
      { Position: 0.105, Value: "#f03e18" },
      { Position: 0.44, Value: "#f03e18" },
      { Position: 0.455, Value: "#f5f7fc" },
      { Position: 0.585, Value: "#f5f7fc" },
      { Position: 0.60, Value: "#f03e18" },
      { Position: 0.705, Value: "#f03e18" },
      { Position: 0.72, Value: "#f5f7fc" },
      { Position: 0.85, Value: "#f5f7fc" },
      { Position: 0.865, Value: "#f03e18" },
      { Position: 1.0, Value: "#f03e18" },
    ],
  };
  Material.Channels.roughness = {
    Source: "Gradient",
    Fill: 0.3,
    Axis: "V",
    Repeat: 1,
    Component: "R",
    Stops: [
      { Position: 0.0, Value: 0.8 },
      { Position: 0.09, Value: 0.8 },
      { Position: 0.11, Value: 0.3 },
      { Position: 0.44, Value: 0.3 },
      { Position: 0.46, Value: 0.12 },
      { Position: 0.58, Value: 0.12 },
      { Position: 0.60, Value: 0.3 },
      { Position: 0.70, Value: 0.3 },
      { Position: 0.72, Value: 0.12 },
      { Position: 0.85, Value: 0.12 },
      { Position: 0.87, Value: 0.3 },
      { Position: 1.0, Value: 0.3 },
    ],
  };
  Material.Channels.coat = { Source: "Fill", Fill: 0.32, Axis: "U", Repeat: 1, Component: "R", Stops: [] };
  Material.Channels.coatRoughness = { Source: "Fill", Fill: 0.16, Axis: "U", Repeat: 1, Component: "R", Stops: [] };
  return Material;
}

export function BuildNeutralPrimitiveMaterial(Hex = "#d2cfca", Roughness = 0.32, Metallic = 0.0) {
  const Material = DefaultMaterial(Metallic > 0.5 ? "Metal" : "Standard");
  Material.Channels.colour = { Source: "Fill", Fill: Hex, Axis: "U", Repeat: 1, Component: "RGB", Stops: [] };
  Material.Channels.roughness = { Source: "Fill", Fill: Roughness, Axis: "U", Repeat: 1, Component: "R", Stops: [] };
  Material.Channels.metallic = { Source: "Fill", Fill: Metallic, Axis: "U", Repeat: 1, Component: "R", Stops: [] };
  return Material;
}

export const WORLD_PLACEABLE_MODELS = Object.freeze([
  {
    Id: "model-traffic-cone",
    TemplateId: "traffic-cone",
    Name: "Traffic Cone",
    ShortLabel: "Traffic Cone",
    Kind: "Models",
    Origin: "Engine",
    Format: ".Mesh",
    PrimitiveShape: "cone",
    Icon: "editor-cone",
    Scale: [0.48, 0.48, 0.78],
    Description: "Automotive tapered safety cone · reflective collar & weighted rubber base",
    Detail:
      "Analytical GTX automotive traffic cone (cone-like cone · 0.48 × 0.48 × 0.78 m) · Safety Orange PVC, retro-reflective white collar & weighted rubber plinth. Drag into the 3D scene to place.",
    BuildMaterial: BuildTrafficConeMaterial,
  },
  {
    Id: "model-cylinder-cone",
    TemplateId: "cylinder-cone",
    Name: "Cylinder Cone",
    ShortLabel: "Cylinder Cone",
    Kind: "Models",
    Origin: "Engine",
    Format: ".Mesh",
    PrimitiveShape: "cylinder-cone",
    Icon: "editor-cylinder",
    Scale: [0.40, 0.40, 0.88],
    Description: "Automotive cylinder-cone delineator post · dual reflective collars",
    Detail:
      "Analytical GTX car lane delineator post (cylinder-like cone · 0.40 × 0.40 × 0.88 m) · steep cylindrical taper, dual high-vis reflective bands & round rubber flange. Drag into the 3D scene to place.",
    BuildMaterial: BuildCylinderConeMaterial,
  },
  {
    Id: "model-cube",
    TemplateId: "cube",
    Name: "Calibration Cube",
    ShortLabel: "Cube",
    Kind: "Models",
    Origin: "Engine",
    Format: ".Mesh",
    PrimitiveShape: "box",
    Icon: "editor-cube",
    Scale: [0.68, 0.68, 0.68],
    Description: "Analytical box primitive · 0.68 m",
    Detail: "Analytical GTX OpenPBR box primitive (0.68 × 0.68 × 0.68 m). Drag into the 3D scene to place.",
    BuildMaterial: () => BuildNeutralPrimitiveMaterial("#d4d0c8", 0.34, 0.0),
  },
  {
    Id: "model-sphere",
    TemplateId: "sphere",
    Name: "Look-Dev Sphere",
    ShortLabel: "Sphere",
    Kind: "Models",
    Origin: "Engine",
    Format: ".Mesh",
    PrimitiveShape: "sphere",
    Icon: "editor-sphere",
    Scale: [0.68, 0.68, 0.68],
    Description: "Analytical sphere primitive · Ø 0.68 m",
    Detail: "Analytical GTX OpenPBR sphere primitive (Ø 0.68 m). Drag into the 3D scene to place.",
    BuildMaterial: () => BuildNeutralPrimitiveMaterial("#dce2ea", 0.14, 0.85),
  },
  {
    Id: "model-cylinder",
    TemplateId: "cylinder",
    Name: "Studio Cylinder",
    ShortLabel: "Cylinder",
    Kind: "Models",
    Origin: "Engine",
    Format: ".Mesh",
    PrimitiveShape: "cylinder",
    Icon: "editor-cylinder",
    Scale: [0.62, 0.62, 0.82],
    Description: "Analytical capped cylinder · 0.62 × 0.82 m",
    Detail: "Analytical GTX capped cylinder primitive (0.62 × 0.62 × 0.82 m). Drag into the 3D scene to place.",
    BuildMaterial: () => BuildNeutralPrimitiveMaterial("#cbc8c2", 0.36, 0.0),
  },
  {
    Id: "model-torus",
    TemplateId: "torus",
    Name: "Shader Torus",
    ShortLabel: "Torus",
    Kind: "Models",
    Origin: "Engine",
    Format: ".Mesh",
    PrimitiveShape: "torus",
    Icon: "editor-torus",
    Scale: [0.76, 0.76, 0.24],
    Description: "Analytical torus ring · Ø 0.76 m",
    Detail: "Analytical GTX ray-marched torus primitive (0.76 × 0.76 × 0.24 m). Drag into the 3D scene to place.",
    BuildMaterial: () => BuildNeutralPrimitiveMaterial("#c88954", 0.24, 0.75),
  },
  ...WORLD_PROP_CATALOG.map((Prop) => ({
    Id: `model-prop-${Prop.PropId}`,
    TemplateId: `prop-${Prop.PropId}`,
    PropId: Prop.PropId,
    Name: Prop.Name,
    ShortLabel: Prop.Name,
    Category: Prop.Category,
    Panel: Prop.Panel,
    Kind: "Models",
    Origin: "Engine",
    Format: Prop.Panel === "camera" ? ".Camera" : Prop.Panel === "light" ? ".Light" : ".Prop",
    PrimitiveShape: "box",
    Icon: Prop.Icon || "editor-cube",
    HalfHeightZ: Prop.HalfHeightZ,
    Scale: Prop.NominalScale || [1, 1, 1],
    DefaultFocalMm: Prop.DefaultFocalMm,
    LightEmitter: Prop.LightEmitter || null,
    Description: Prop.Description,
    Detail: `${Prop.Category} · ${Prop.Description} Drag into the 3D viewport or double-click to place.`,
    BuildMaterial: () => BuildNeutralPrimitiveMaterial("#d4d0c8", 0.34, 0.0),
  })),
]);

export function BuildActorPayloadFromModelSpec(Spec) {
  if (Spec.PropId) {
    const PropDef = GetWorldPropById(Spec.PropId) || Spec;
    const Panel = PropDef.Panel || Spec.Panel || "geometry";
    const Parent =
      Panel === "camera" ? "cameras" : Panel === "light" ? "lighting" : "showcase";
    const Properties = {
      PropId: Spec.PropId,
      Rotation: [0, 0, 0],
      Scale: [...(PropDef.NominalScale || [1, 1, 1])],
      "Cast shadows": true,
      GI: true,
    };
    if (Panel === "camera") {
      Properties["Focal Length"] = PropDef.DefaultFocalMm || 35;
      Properties["Sensor Width"] = 36;
      Properties.Aperture = 2.8;
      Properties["Subject Distance"] = 3.5;
    } else if (Panel === "light" && PropDef.LightEmitter) {
      const Em = PropDef.LightEmitter;
      Properties.Intensity = 8.5;
      Properties.EmittedRadiance = [...Em.Radiance];
      Properties.ReferenceInspector = {
        Type: "LuminousSurface",
        Properties: {
          lumens: Em.Lumens || 3000,
          dimmer: 1,
          color: Em.ColorHex || "#fff4d6",
          temperature: 5500,
          useTemp: false,
          width: Em.Width,
          height: Em.Height,
          twoSided: false,
          spread: 160,
          castShadows: true,
          shadowSoftness: 1,
          affectSpecular: true,
        },
      };
    }
    return {
      ActorKind: Panel,
      Label: Spec.Name || PropDef.Name,
      HalfHeightZ: Math.max(0.04, Number(PropDef.HalfHeightZ ?? Spec.HalfHeightZ ?? 0.4)),
      Template: {
        Id: Spec.TemplateId || Spec.Id,
        Name: Spec.Name || PropDef.Name,
        Panel,
        PropId: Spec.PropId,
        Icon: Spec.Icon || PropDef.Icon || "editor-cube",
        Parent,
        Preview: true,
        Description: Spec.Description || PropDef.Description || "Prop",
      },
      Properties,
    };
  }

  const Scale = Spec.Scale ? [...Spec.Scale] : [0.7, 0.7, 0.7];
  const Material = typeof Spec.BuildMaterial === "function" ? Spec.BuildMaterial() : DefaultMaterial("Standard");
  return {
    ActorKind: "geometry",
    Label: Spec.Name,
    HalfHeightZ: Math.max(0.05, Math.abs(Number(Scale[2]) || 0.7) * 0.5),
    Template: {
      Id: Spec.TemplateId || Spec.Id,
      Name: Spec.Name,
      Panel: "geometry",
      Icon: Spec.Icon || "editor-cube",
      PrimitiveShape: Spec.PrimitiveShape || "box",
      Parent: "showcase",
      Preview: true,
      Description: Spec.Description || "Geometry",
    },
    Properties: {
      Rotation: [0, 0, 0],
      Scale,
      "Cast shadows": true,
      GI: true,
      Material,
    },
  };
}

export function BuildActorPayloadFromConstructItem(Item) {
  if (!Item) return null;
  if (Item.PropId) {
    const PropSpec = WORLD_PLACEABLE_MODELS.find((M) => M.PropId === Item.PropId);
    if (PropSpec) {
      return BuildActorPayloadFromModelSpec({ ...PropSpec, Name: Item.Name || PropSpec.Name });
    }
  }
  if (Item.PrimitiveShape === "cone" || Item.Id === "traffic-cone") {
    return BuildActorPayloadFromModelSpec(WORLD_PLACEABLE_MODELS[0]);
  }
  if (Item.PrimitiveShape === "cylinder-cone" || Item.Id === "cylinder-cone") {
    return BuildActorPayloadFromModelSpec(WORLD_PLACEABLE_MODELS[1]);
  }
  if (Item.Panel === "camera") {
    const FallbackCam = WORLD_PLACEABLE_MODELS.find((M) => M.PropId === "engine_camera");
    if (FallbackCam) {
      return BuildActorPayloadFromModelSpec({ ...FallbackCam, Name: Item.Name || "Engine Camera" });
    }
  }
  if (Item.Panel === "geometry") {
    const Shape =
      Item.PrimitiveShape ||
      (Item.Icon === "editor-sphere"
        ? "sphere"
        : Item.Icon === "editor-cylinder"
          ? "cylinder"
          : Item.Icon === "editor-cone"
            ? "cone"
            : Item.Icon === "editor-torus"
              ? "torus"
              : "box");
    const Match = WORLD_PLACEABLE_MODELS.find((M) => M.PrimitiveShape === Shape && !M.PropId);
    if (Match && (Item.Id === Match.TemplateId || Item.Id === "cone")) {
      return BuildActorPayloadFromModelSpec({ ...Match, Name: Item.Name });
    }
    const Scale = Shape === "torus" ? [0.76, 0.76, 0.24] : [0.72, 0.72, 0.72];
    return {
      ActorKind: "geometry",
      Label: Item.Name,
      HalfHeightZ: Scale[2] * 0.5,
      Template: {
        ...Item,
        PrimitiveShape: Shape,
        Parent: Item.Parent || "showcase",
        Preview: true,
      },
      Properties: {
        Rotation: [0, 0, 0],
        Scale,
        "Cast shadows": true,
        GI: true,
        Material: BuildNeutralPrimitiveMaterial("#d2cfca", 0.32, 0.0),
      },
    };
  }
  if (Item.Panel === "force") {
    return {
      ActorKind: "force",
      Label: Item.Name,
      HalfHeightZ: 0.45,
      Template: {
        ...Item,
        Parent: "forces",
        Preview: true,
      },
      Properties: {
        Rotation: [0, 0, 0],
        Scale: [1, 1, 1],
        Force: Item.DefaultForce ? JSON.parse(JSON.stringify(Item.DefaultForce)) : undefined,
      },
    };
  }
  if (Item.Panel === "light" || Item.Category === "Lighting") {
    return {
      ActorKind: "light",
      Label: Item.Name,
      HalfHeightZ: 1.4,
      Template: {
        ...Item,
        Parent: Item.Parent || "lighting",
        Preview: true,
      },
      Properties: {
        Rotation: [0, 0, 0],
        Intensity: 7.5,
        Width: 1.4,
        Height: 1.0,
      },
    };
  }
  return null;
}

export function BuildActorPayloadFromAsset(Asset) {
  if (!Asset) return null;
  if (Asset.Kind === "Models") {
    const SpecMatch = WORLD_PLACEABLE_MODELS.find(
      (M) =>
        M.Id === Asset.Id ||
        (Asset.PropId && M.PropId === Asset.PropId) ||
        (!Asset.PropId && !M.PropId && M.PrimitiveShape === Asset.PrimitiveShape),
    );
    if (SpecMatch) {
      return BuildActorPayloadFromModelSpec({
        ...SpecMatch,
        Name: Asset.Name || SpecMatch.Name,
      });
    }
    return BuildActorPayloadFromModelSpec({
      Id: Asset.Id,
      TemplateId: "sphere",
      Name: Asset.Name || "Model Mesh",
      PrimitiveShape: Asset.PrimitiveShape || "sphere",
      Icon: "editor-sphere",
      Scale: Asset.Scale || [0.76, 0.76, 0.76],
      Description: Asset.Detail || "Model asset",
      BuildMaterial: () => BuildNeutralPrimitiveMaterial("#d8dde4", 0.22, 0.25),
    });
  }
  if (Asset.Kind === "Forces") {
    return {
      ActorKind: "force",
      Label: Asset.Name,
      HalfHeightZ: 0.45,
      Template: {
        Id: `asset-template-${Asset.Id}`,
        Name: Asset.Name,
        Panel: "force",
        ForceModel: Asset.Force?.Model || "LinearWind",
        Icon: `force-model:${Asset.Force?.Model || "LinearWind"}`,
        Parent: "forces",
        Preview: true,
        Description: Asset.Detail || "Force / VectorField",
      },
      Properties: {
        Rotation: Array.isArray(Asset.Rotation) ? [...Asset.Rotation] : [0, 0, 0],
        Scale: [1, 1, 1],
        Force: Asset.Force ? JSON.parse(JSON.stringify(Asset.Force)) : undefined,
      },
    };
  }
  if (Asset.Kind === "Materials" && Asset.Material) {
    return {
      ActorKind: "material",
      Label: Asset.Name,
      Material: JSON.parse(JSON.stringify(Asset.Material)),
    };
  }
  return null;
}
