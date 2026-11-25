# Asset Viewer prototype

Run `python3 Experimental/ComponentViewer/ServeAssetViewer.py` and open port 8096.
Select a scene component or socket and use **Add → Instance** to create an empty placeholder beneath it.
Import a file in the Inspector or drag an asset from the bottom Asset Browser onto the placeholder.
The left Content panel has been removed. Assets and edits are in-memory for this session.

The asset Inspector adapts the Geometry Compiler Studio's Icon, UV Layout, SDF Volume and Geometry cards.
Icon capture renders the selected asset to a fitted offscreen camera; the viewport guide is approximate,
and Live View follows its angle with lit/normals shading. UV opens the Studio's island editing workspace;
changes are applied to the asset when edited or closed. SDF opens the Studio sparse 8×8×8 volume workspace;
its inspector slice supports axis, mip and depth controls. Geometry opens the Studio transform workspace,
and Apply rebuilds the asset and marks dependent bakes stale. These are application workspaces, not frames.

The Cluster LOD card opens a dedicated, integrated single-asset workspace using Geometry Compiler Studio’s WebGL2 viewport. Build or rebuild the shared hierarchy, select automatic or forced LODs in the outliner, inspect picked clusters, and switch between shaded, clusters, groups, LOD level, triangles and error debug views. Drag to orbit, Shift-drag or right-drag to pan, scroll to zoom, and press F to fit. WebGL2 is required. This experimental hierarchy is not a Nanite runtime.
Material editing embeds Frontier World Mirror's original MaterialPanel and shader-ball card. The card stores its full
channel/source drafts; the component viewport currently renders constant colour, roughness and metallic only. Fracture uses the Fracture Editor's Voronoi recipe and a projected proxy-hull preview; imported meshes
are not fractured directly and no runtime solver is connected.

The browser includes the Frontier World Mirror model catalog, tessellated only when dropped. These are
approximate polygonal representations of its analytic models, with simplified materials; the generic browser
card icon is not a baked thumbnail. Files can also be dropped into the browser or imported with its button.
FBX is not supported; glTF must be embedded (or use GLB).

Smoke test: `JSDOM_DIR=/tmp/smoke node Experimental/ComponentViewer/Scripts/SmokeAssetViewer.mjs`.

Imported OBJ face texture coordinates and glTF/GLB TEXCOORD_0 are shown immediately when present; glTF
base-colour, metallic and roughness factors are also read. The UV workspace's automatic unwrap replaces authored
UVs only if edited or repacked. OBJ MTL files, glTF textures, external buffers and embedded compiled icons are not
yet imported; normal OBJ/glTF do not define a standard compiled asset icon. Icons captured in this session are shown.

The World Material bundle is built from `Experimental/FrontierWorldMirror/MaterialBridge.jsx` and the original
World Mirror MaterialPanel with React 18 and esbuild 0.25. `ShaderBall.mesh` is extracted from the linked branch's
World Mirror `index.html` NativeAssets payload. To rebuild after editing the card: install react, react-dom and
esbuild, then bundle that entry as ESM; the viewer loads the generated JS and CSS in `ComponentViewer/vendor`.
