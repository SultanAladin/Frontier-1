// HTML authoring model: a bounded XZ slice, not a native atmospheric solver.
export const WindTypes = ["Directional", "Gust", "Tornado", "Radial"];
const Clamp = (Value, Min, Max, Default) =>
  Number.isFinite(Number(Value))
    ? Math.max(Min, Math.min(Max, Number(Value)))
    : Default;
export function NewWindComponent(Type = "Directional") {
  return {
    Id: crypto.randomUUID(),
    Name: Type,
    Type,
    Enabled: true,
    X: 0,
    Z: 0,
    Radius: 260,
    Strength: Type === "Tornado" ? 22 : 7,
    Bearing: 90,
    Frequency: 0.3,
  };
}
export function ResolveWind(Values = {}) {
  const Saved = Values.WindField;
  const Width = Clamp(Saved?.Width, 100, 10000, 1000),
    Depth = Clamp(Saved?.Depth, 100, 10000, 1000);
  const Components = Array.isArray(Saved?.Components)
    ? Saved.Components
    : [
        {
          Id: "base",
          Name: "Prevailing wind",
          Type: "Directional",
          Enabled: true,
          Strength: Values.Speed ?? 7,
          Bearing: Values.Bearing ?? 250,
        },
        {
          Id: "gust",
          Name: "Passing gust",
          Type: "Gust",
          Enabled: true,
          Strength: (Values.Speed ?? 7) * (Values.Gust ?? 0.25),
          Radius: 450,
          Bearing: Values.Bearing ?? 250,
          Frequency: 0.3,
        },
      ];
  const Ids = new Set();
  return {
    Width,
    Depth,
    Components: Components.filter((Item) => Item && typeof Item === "object")
      .slice(0, 64)
      .map((Item, Index) => {
        let Id =
          typeof Item.Id === "string" && Item.Id
            ? Item.Id
            : `component-${Index}`;
        while (Ids.has(Id)) Id += "-copy";
        Ids.add(Id);
        return {
          Id,
          Name: String(Item.Name || Item.Type || "Wind").slice(0, 80),
          Type: WindTypes.includes(Item.Type) ? Item.Type : "Directional",
          Enabled: Item.Enabled !== false,
          X: Clamp(Item.X, -Width / 2, Width / 2, 0),
          Z: Clamp(Item.Z, -Depth / 2, Depth / 2, 0),
          Radius: Clamp(Item.Radius, 10, 10000, 260),
          Strength: Clamp(Item.Strength, 0, 100, 7),
          Bearing: Clamp(Item.Bearing, 0, 360, 90),
          Frequency: Clamp(Item.Frequency, 0, 5, 0.3),
        };
      }),
  };
}
// Linear superposition of every enabled component, in metres/second.
export function EvaluateWind(Field, X, Z, Time = 0) {
  let VX = 0,
    VZ = 0;
  for (const Part of Field.Components) {
    if (!Part.Enabled || !Part.Strength) continue;
    const DX = X - Part.X,
      DZ = Z - Part.Z,
      Distance = Math.hypot(DX, DZ),
      Radius = Distance / Part.Radius;
    const Falloff = Math.max(0, 1 - Radius * Radius) ** 2;
    const Angle = (Part.Bearing * Math.PI) / 180;
    if (Part.Type === "Directional" || Part.Type === "Gust") {
      const Amount =
        Part.Strength *
        (Part.Type === "Gust"
          ? Falloff *
            (0.65 + 0.35 * Math.sin(Time * 2 * Math.PI * Part.Frequency))
          : 1);
      VX += Math.sin(Angle) * Amount;
      VZ -= Math.cos(Angle) * Amount;
    } else if (Distance > 1e-6) {
      const Amount = Part.Strength * Falloff * Math.min(1, Radius * 3);
      if (Part.Type === "Tornado") {
        VX += ((-DZ - 0.18 * DX) / Distance) * Amount;
        VZ += ((DX - 0.18 * DZ) / Distance) * Amount;
      } else {
        VX += (DX / Distance) * Amount;
        VZ += (DZ / Distance) * Amount;
      }
    }
  }
  return [VX, VZ];
}
