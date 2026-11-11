import {
  ChooseMaterialType,
  MaterialTypes,
  ResolveMaterial,
} from "./MaterialSpecification.js";
export async function AssetFile(Id, Value, Remove = false) {
  return new Promise((Resolve, Reject) => {
    const Request = indexedDB.open("Frontier.ProjectZero.Assets", 1);
    Request.onupgradeneeded = () => Request.result.createObjectStore("Files");
    Request.onerror = () => Reject(Request.error);
    Request.onsuccess = () => {
      const Database = Request.result,
        Transaction = Database.transaction(
          "Files",
          Value || Remove ? "readwrite" : "readonly",
        ),
        Store = Transaction.objectStore("Files");
      const Operation = Remove
        ? Store.delete(Id)
        : Value
          ? Store.put(Value, Id)
          : Store.get(Id);
      let Result;
      Operation.onsuccess = () => (Result = Operation.result);
      Transaction.oncomplete = () => {
        Database.close();
        Resolve(Result);
      };
      Transaction.onerror = () => {
        Database.close();
        Reject(Transaction.error);
      };
    };
  });
}
export function EngineAssets() {
  return [
    ...MaterialTypes.map((Type, Index) => ({
      Id: "engine-material-" + Index,
      Name: Type + " surface",
      Kind: "Materials",
      Origin: "Engine",
      Material: {
        ...ChooseMaterialType(ResolveMaterial({}), Type),
        Name: Type + " surface",
      },
      Detail: "Built-in HTML authoring preset. Not a resolved native slab.",
    })),
    ...["Luna", "Ember", "Glacier", "Sulfur", "Shroud", "Shard"].map(
      (Name, Index) => ({
        Id: "engine-image-" + Index,
        Name: Name + " albedo",
        Kind: "Images",
        Origin: "Engine",
        Data: window.NativeAssets.Moons[Index],
        Detail:
          "Embedded 512 × 256 preview of the engine celestial image; not the full 2K source.",
      }),
    ),
    ...Object.entries(window.NativeAssets.Fonts || {}).map(([Name, Data]) => ({
      Id: "engine-font-" + Name,
      Name: "DM Sans " + Name,
      Kind: "Fonts",
      Origin: "Engine",
      Data,
      Detail:
        "Shipped editor font. Preview only; does not change editor typography.",
    })),
    ...Object.entries(window.NativeAssets.Icons).map(([Name, Data]) => ({
      Id: "engine-icon-" + Name,
      Name,
      Kind: "Icons",
      Origin: "Engine",
      Data,
      Detail: "Shipped native icon artwork. See embedded icon notices.",
    })),
    {
      Id: "engine-shaderball",
      Name: "Shader ball",
      Kind: "Models",
      Origin: "Engine",
      Detail:
        "Existing shader-ball mesh · 35,897 vertices · 67,832 triangles. Native SBM1 container.",
    },
  ];
}
export const AssetKinds = [
  "All",
  "Materials",
  "Images",
  "Fonts",
  "Models",
  "Icons",
  "Code",
  "Other",
];
export function FileKind(Name) {
  const Extension = Name.split(".").at(-1).toLowerCase();
  return ["png", "jpg", "jpeg", "webp", "gif"].includes(Extension)
    ? "Images"
    : ["ttf", "otf", "woff", "woff2"].includes(Extension)
      ? "Fonts"
      : ["obj", "gltf", "glb", "fbx", "mesh"].includes(Extension)
        ? "Models"
        : ["slang", "glsl", "hlsl", "cpp", "h", "wgsl"].includes(Extension)
          ? "Code"
          : "Other";
}
export async function ImportAsset(File) {
  if (File.size > 16 * 1024 * 1024)
    throw Error(File.name + ": maximum file size is 16 MB.");
  const Asset = {
    Id: "asset-" + crypto.randomUUID(),
    Name: File.name,
    Kind: FileKind(File.name),
    Origin: "Imported",
    Size: File.size,
    Type: File.type,
    Detail:
      "Original file stored in this browser. Native asset processing is not connected.",
  };
  if (File.name.toLowerCase().endsWith(".material.json")) {
    if (File.size > 2 * 1024 * 1024)
      throw Error("Material JSON must be below 2 MB.");
    const Content = JSON.parse(await File.text());
    if (Content.Format !== "FrontierMaterial.v1" || !Content.Material?.Channels)
      throw Error("Not a FrontierMaterial.v1 document.");
    Asset.Kind = "Materials";
    Asset.Material = ResolveMaterial({ Material: Content.Material });
    Asset.Name = Asset.Material.Name;
  }
  if (Asset.Kind === "Images") {
    const Url = URL.createObjectURL(File);
    try {
      const Image = new window.Image();
      Image.src = Url;
      await Image.decode();
      Asset.Width = Image.width;
      Asset.Height = Image.height;
      const Canvas = document.createElement("canvas"),
        Ratio = Math.min(1, 96 / Math.max(Image.width, Image.height));
      Canvas.width = Math.max(1, Math.round(Image.width * Ratio));
      Canvas.height = Math.max(1, Math.round(Image.height * Ratio));
      Canvas.getContext("2d").drawImage(
        Image,
        0,
        0,
        Canvas.width,
        Canvas.height,
      );
      Asset.Thumbnail = Canvas.toDataURL("image/png");
    } finally {
      URL.revokeObjectURL(Url);
    }
  }
  await AssetFile(Asset.Id, File);
  return Asset;
}
export async function DownloadAsset(Asset) {
  let BlobData;
  if (Asset.Kind === "Materials")
    BlobData = new Blob(
      [
        JSON.stringify(
          { Format: "FrontierMaterial.v1", Material: Asset.Material },
          null,
          2,
        ),
      ],
      { type: "application/json" },
    );
  else if (Asset.Id === "engine-shaderball")
    BlobData = new Blob(
      [
        Uint8Array.from(atob(window.NativeAssets.ShaderBall), (Character) =>
          Character.charCodeAt(0),
        ),
      ],
      { type: "application/octet-stream" },
    );
  else if (Asset.Data) BlobData = await (await fetch(Asset.Data)).blob();
  else BlobData = await AssetFile(Asset.Id);
  if (!BlobData)
    throw Error(
      "File bytes are missing in this browser. Reimport the original asset.",
    );
  const Link = document.createElement("a"),
    Url = URL.createObjectURL(BlobData);
  Link.href = Url;
  Link.download =
    Asset.Kind === "Materials"
      ? Asset.Name + ".material.json"
      : Asset.Id === "engine-shaderball"
        ? "ShaderBall.mesh"
        : Asset.Origin === "Engine"
          ? Asset.Name +
            (Asset.Kind === "Images"
              ? ".jpg"
              : Asset.Kind === "Fonts"
                ? ".ttf"
                : ".svg")
          : Asset.Name;
  Link.click();
  setTimeout(() => URL.revokeObjectURL(Url), 1000);
}
export function RestoreAssets(Records) {
  const Seen = new Set();
  return (Array.isArray(Records) ? Records : [])
    .slice(0, 256)
    .filter((Asset) => {
      if (
        !Asset ||
        typeof Asset.Id !== "string" ||
        !Asset.Id.startsWith("asset-") ||
        Seen.has(Asset.Id)
      )
        return false;
      Seen.add(Asset.Id);
      return true;
    })
    .map((Asset) => ({
      Id: Asset.Id,
      Name: String(Asset.Name || "Untitled").slice(0, 100),
      Kind:
        AssetKinds.includes(Asset.Kind) && Asset.Kind !== "All"
          ? Asset.Kind
          : "Other",
      Origin: Asset.Origin === "Project" ? "Project" : "Imported",
      Size: +Asset.Size || 0,
      Width: +Asset.Width || 0,
      Height: +Asset.Height || 0,
      Detail:
        typeof Asset.Detail === "string"
          ? Asset.Detail
          : "Restored browser asset",
      ...(Asset.Kind === "Materials"
        ? { Material: ResolveMaterial({ Material: Asset.Material }) }
        : {}),
      ...(/^data:image\/(png|jpeg|webp);base64,[A-Za-z0-9+/=]+$/.test(
        Asset.Thumbnail,
      ) && Asset.Thumbnail.length < 100000
        ? { Thumbnail: Asset.Thumbnail }
        : {}),
    }));
}
