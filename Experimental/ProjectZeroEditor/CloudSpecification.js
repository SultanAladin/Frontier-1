import Native from "./NativePanels.json";
export const CloudProperties = {
  Coverage: "coverage",
  Base: "altitude",
  Density: "density",
};
export function CloudValue(Values, Name, Panel = "clouds") {
  if (Panel === "local-cloud" && ["Base", "Thickness"].includes(Name)) {
    const Centre = CloudValue(Values, "Centre", Panel),
      Half = CloudValue(Values, "Half Size", Panel);
    return Name === "Base" ? Centre[2] - Half[2] : Half[2] * 2;
  }
  if (Values[Name] !== undefined) return Values[Name];
  const Imported =
    Values.ReferenceInspector?.Properties?.[CloudProperties[Name]];
  if (Imported !== undefined)
    return Name === "Density" ? Imported * 4 : Imported;
  return Native.Panels[Panel].find((Field) => Field.Label === Name)?.Default;
}
export function CloudReference(Values, Panel = "clouds") {
  return Object.fromEntries(
    Object.entries(CloudProperties).map(([Name, Key]) => [
      Key,
      CloudValue(Values, Name, Panel) / (Name === "Density" ? 4 : 1),
    ]),
  );
}

export function CloudEdit(Values, Name, Next, Panel = "clouds") {
  if (Panel === "local-cloud" && Name === "Base") {
    const Centre = [...CloudValue(Values, "Centre", Panel)];
    Centre[2] = Math.max(
      -100000,
      Math.min(100000, Next + CloudValue(Values, "Half Size", Panel)[2]),
    );
    return { Centre };
  }
  if (Panel === "local-cloud" && Name === "Thickness") {
    const Half = [...CloudValue(Values, "Half Size", Panel)];
    Half[2] = Math.max(0.001, Math.min(100000, Next / 2));
    return { "Half Size": Half };
  }
  return { [Name]: Next };
}
