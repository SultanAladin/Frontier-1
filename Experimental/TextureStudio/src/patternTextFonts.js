import opentype from "opentype.js";
import dmRegular from "@fontsource/dm-sans/files/dm-sans-latin-400-normal.woff?url";
import dmBold from "@fontsource/dm-sans/files/dm-sans-latin-600-normal.woff?url";
import spaceRegular from "@fontsource/space-grotesk/files/space-grotesk-latin-400-normal.woff?url";
import spaceBold from "@fontsource/space-grotesk/files/space-grotesk-latin-600-normal.woff?url";
const sources = {
  "DM Sans-400": dmRegular,
  "DM Sans-600": dmBold,
  "Space Grotesk-400": spaceRegular,
  "Space Grotesk-600": spaceBold,
};
const fonts = new Map(),
  pending = new Map();
export const patternTextFont = (family = "DM Sans", weight = 400) =>
  fonts.get(`${family}-${weight}`);
export function loadPatternTextFonts() {
  return Promise.all(
    Object.entries(sources).map(([key, url]) => {
      if (!pending.has(key))
        pending.set(
          key,
          fetch(url)
            .then((r) => {
              if (!r.ok)
                throw new Error("Unable to load the bundled text fonts.");
              return r.arrayBuffer();
            })
            .then((data) => {
              const font = opentype.parse(data);
              fonts.set(key, font);
              return font;
            })
            .catch((e) => {
              pending.delete(key);
              throw e;
            }),
        );
      return pending.get(key);
    }),
  );
}
