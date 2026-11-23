export const EditorCamera = {
  Id: "camera",
  Name: "Editor Camera",
  Panel: "camera",
  Icon: "camera",
  Parent: "cameras",
  Description: "Permanent editor navigation camera · not a scene render camera",
};
export const IsEditorCamera = (Row) => Row?.Id === EditorCamera.Id;
// Normalize every scene load and structural edit, not just the remove menu.
// The original reserved camera ID keeps existing optics/property drafts.
export function EnsureEditorCamera(Rows = []) {
  const Source = (Array.isArray(Rows) ? Rows : []).filter(
    (Row) => Row && typeof Row === "object",
  );
  const Group = Source.find((Row) => Row.Id === "cameras");
  const Result = Source.filter(
    (Row) => Row.Id !== "camera" && Row.Id !== "cameras",
  );
  const First = Result.findIndex((Row) => Row.Panel === "camera"),
    Previous = Source.findIndex((Row) => Row.Id === "cameras");
  const Index = Math.min(
    First < 0 ? Result.length : First,
    Previous < 0 ? 0 : Previous,
  );
  Result.splice(
    Index,
    0,
    {
      ...Group,
      Id: "cameras",
      Name: Group?.Name || "Cameras",
      Panel: "group",
      Icon: "camera",
      Parent: null,
      Description: "Editor and scene cameras",
    },
    { ...EditorCamera },
  );
  return Result;
}

// Only the legacy built-in Sky alias shares Atmosphere's full inspector. User-created skies are untouched.
export function ConsolidateAtmosphere(Rows = []) {
  const Sky = Rows.find(
    (Row) => Row.Id === "sky" && Row.Panel === "atmosphere",
  );
  if (!Sky) return Rows;
  const Atmosphere = Rows.find((Row) => Row.Id === "atmosphere");
  if (Atmosphere && Atmosphere.Panel !== "atmosphere") return Rows;
  return Rows.flatMap((Row) =>
    Row === Sky
      ? Atmosphere
        ? []
        : [
            {
              ...Sky,
              Id: "atmosphere",
              Name: "Atmosphere",
              Description: "Procedural sky and atmospheric scattering",
            },
          ]
      : [{ ...Row, Parent: Row.Parent === "sky" ? "atmosphere" : Row.Parent }],
  );
}
export function ConsolidateAtmosphereValues(Values = {}, Rows = []) {
  const Atmosphere = Rows.find((Row) => Row.Id === "atmosphere");
  if (Atmosphere && Atmosphere.Panel !== "atmosphere") return { ...Values };
  // Keep the legacy record in exported values, including conflicting authored properties.
  return Values.sky &&
    Rows.some((Row) => Row.Id === "sky" && Row.Panel === "atmosphere")
    ? { ...Values, atmosphere: { ...Values.sky, ...Values.atmosphere } }
    : { ...Values };
}

export function ConsolidateAtmosphereFlags(Flags = {}, Rows = []) {
  const SkyOnly =
    !Rows.some((Row) => Row.Id === "atmosphere") &&
    Rows.some((Row) => Row.Id === "sky" && Row.Panel === "atmosphere");
  return SkyOnly ? { ...Flags, atmosphere: Flags.sky ?? false } : { ...Flags };
}
