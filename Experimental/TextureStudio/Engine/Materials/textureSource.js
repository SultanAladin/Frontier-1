// Small, self-contained raster sources, not allocated painting atlases. Source
// images are deliberately resampled so project/history storage remains bounded.
export const TEXTURE_SOURCE_EDGE = 256;
export const TEXTURE_SOURCE_LIMIT = 300000;
export function normalizeTextureSource(raw) {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return null;
  if (
    typeof raw.dataUrl !== "string" ||
    raw.dataUrl.length > TEXTURE_SOURCE_LIMIT ||
    !/^data:image\/png;base64,[A-Za-z0-9+/]+={0,2}$/.test(raw.dataUrl)
  )
    return null;
  const width = Number(raw.width),
    height = Number(raw.height);
  if (
    !Number.isInteger(width) ||
    !Number.isInteger(height) ||
    width < 1 ||
    height < 1 ||
    width > TEXTURE_SOURCE_EDGE ||
    height > TEXTURE_SOURCE_EDGE
  )
    return null;
  try {
    const encoded = raw.dataUrl.slice(raw.dataUrl.indexOf(",") + 1),
      header = Uint8Array.from(atob(encoded.slice(0, 64)), (c) =>
        c.charCodeAt(0),
      ),
      actual = pngDimensions(header);
    if (!actual || actual[0] !== width || actual[1] !== height) return null;
  } catch {
    return null;
  }
  return {
    name:
      (typeof raw.name === "string"
        ? raw.name.replace(/[\u0000-\u001f\u007f]/g, "").slice(0, 80)
        : "") || "Texture source",
    width,
    height,
    dataUrl: raw.dataUrl,
    ...(["paint", "material-generator", "gradient"].includes(raw.origin)
      ? { origin: raw.origin }
      : {}),
  };
}
function pngDimensions(bytes) {
  if (
    bytes.length < 24 ||
    bytes[0] !== 137 ||
    bytes[1] !== 80 ||
    bytes[2] !== 78 ||
    bytes[3] !== 71 ||
    bytes[4] !== 13 ||
    bytes[5] !== 10 ||
    bytes[6] !== 26 ||
    bytes[7] !== 10 ||
    String.fromCharCode(...bytes.slice(12, 16)) !== "IHDR"
  )
    return null;
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return [view.getUint32(16), view.getUint32(20)];
}
function jpegDimensions(bytes) {
  if (bytes[0] !== 255 || bytes[1] !== 216) return null;
  let offset = 2;
  while (offset + 4 < bytes.length) {
    if (bytes[offset] !== 255) {
      offset++;
      continue;
    }
    const marker = bytes[offset + 1];
    offset += 2;
    if (marker === 217 || marker === 218) break;
    if (
      marker === 0 ||
      marker === 216 ||
      marker === 1 ||
      (marker >= 208 && marker <= 215)
    )
      continue;
    if (offset + 2 > bytes.length) break;
    const size = bytes[offset] * 256 + bytes[offset + 1];
    if (size < 2 || offset + size > bytes.length) break;
    if (
      [
        192, 193, 194, 195, 197, 198, 199, 201, 202, 203, 205, 206, 207,
      ].includes(marker) &&
      size >= 7
    )
      return [
        bytes[offset + 5] * 256 + bytes[offset + 6],
        bytes[offset + 3] * 256 + bytes[offset + 4],
      ];
    offset += size;
  }
  throw new Error("Could not read the JPEG dimensions.");
}
function webpDimensions(bytes) {
  if (
    bytes.length < 30 ||
    String.fromCharCode(...bytes.slice(0, 4)) !== "RIFF" ||
    String.fromCharCode(...bytes.slice(8, 12)) !== "WEBP"
  )
    return null;
  const tag = String.fromCharCode(...bytes.slice(12, 16));
  if (tag === "VP8X")
    return [
      1 + bytes[24] + (bytes[25] << 8) + (bytes[26] << 16),
      1 + bytes[27] + (bytes[28] << 8) + (bytes[29] << 16),
    ];
  if (tag === "VP8 ")
    return [
      (bytes[26] + (bytes[27] << 8)) & 16383,
      (bytes[28] + (bytes[29] << 8)) & 16383,
    ];
  if (tag === "VP8L" && bytes[20] === 47)
    return [
      1 + bytes[21] + ((bytes[22] & 63) << 8),
      1 + ((bytes[22] >> 6) | (bytes[23] << 2) | ((bytes[24] & 15) << 10)),
    ];
  throw new Error("Could not read the WebP dimensions.");
}
export function rasterDimensions(bytes) {
  const dimensions =
    pngDimensions(bytes) || jpegDimensions(bytes) || webpDimensions(bytes);
  if (!dimensions)
    throw new Error(
      "Import a PNG, JPEG or WebP raster. SVG and executable sources are not accepted here.",
    );
  if (
    dimensions.some((n) => n < 1 || n > 8192) ||
    dimensions[0] * dimensions[1] > 16777216
  )
    throw new Error(
      "Texture inputs are limited to 8192 pixels per edge and 16 million pixels.",
    );
  return dimensions;
}
export async function importTextureSource(file) {
  if (!file || file.size > 4 * 1024 * 1024)
    throw new Error("Texture inputs must be smaller than 4 MB.");
  const bytes = new Uint8Array(await file.arrayBuffer());
  rasterDimensions(bytes);
  const bitmap = await createImageBitmap(new Blob([bytes]));
  try {
    if (
      bitmap.width < 1 ||
      bitmap.height < 1 ||
      bitmap.width > 8192 ||
      bitmap.height > 8192 ||
      bitmap.width * bitmap.height > 16777216
    )
      throw new Error(
        "The decoded texture exceeds the 16-million-pixel input limit.",
      );
    const factor = Math.min(
        1,
        TEXTURE_SOURCE_EDGE / Math.max(bitmap.width, bitmap.height),
      ),
      canvas = document.createElement("canvas");
    canvas.width = Math.max(1, Math.round(bitmap.width * factor));
    canvas.height = Math.max(1, Math.round(bitmap.height * factor));
    canvas
      .getContext("2d")
      .drawImage(bitmap, 0, 0, canvas.width, canvas.height);
    const source = normalizeTextureSource({
      name: file.name,
      width: canvas.width,
      height: canvas.height,
      dataUrl: canvas.toDataURL("image/png"),
    });
    if (!source)
      throw new Error(
        "This source is too large for a portable project. Choose a smaller raster.",
      );
    return source;
  } finally {
    bitmap.close();
  }
}
