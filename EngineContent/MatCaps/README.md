# MatCap Studio atlas

`MatCapStudioAtlas.png` is the inspectable 4×4, 1024×1024 baked preview atlas.
`MatCapStudioAtlas.rgba` is its tightly packed RGBA8 pixel data used by the native Vulkan viewport and Inspector thumbnail texture.
`MatCapStudioCatalog.json` records the slot order and labels.

The pixels are baked from `Experimental/UVEditor/Engine/EngineeringMatcaps.js` using its `SynthesizeMatcapPixels` routine at 256×256 pixels per slot. Slots are row-major, 0–15. The atlas is the shared source for both native viewport sampling and the UI preview menu.
