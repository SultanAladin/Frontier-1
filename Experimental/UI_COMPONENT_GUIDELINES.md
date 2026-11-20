# Experimental Remastered UI Component Guidelines

These guidelines are the shared reference for new and existing interactive UI in `Experimental Remastered/`. They are linked from the repository-root `CLAUDE.md` so Claude checks them before making studio components.

## Reference studio and intent

Use [Vehicle Lab](./AssetInspector/README.md) and its standalone app (`AssetInspector/index.html`, `.css`, and `.js`) as the reference for a clear engineering-studio composition: a component Outliner, a dominant central Stage, and a categorized Inspector. Reuse its layout and interaction principles—not its vehicle-specific labels, physics, or exact dimensions. Respect the purpose and working behavior of each host studio and preserve its existing simulation/rendering engine unless a task explicitly calls for changing it.

For the shared visual language, look at the Particle and Photometric studios alongside Vehicle Lab: neutral charcoal surfaces, rounded cards, pill-shaped controls, restrained borders, readable hierarchy, and flat semantic icon colors.

## Layout and hierarchy

- Keep the primary asset, scene, or simulation visible and visually dominant in a Stage/viewport.
- When appropriate, flank the Stage with a searchable Outliner and a contextual Inspector. Use a compact lower dock for curves, logs, timelines, or other secondary authoring tools; do not let a node graph or settings panel displace the primary preview without a clear task requirement.
- Group related controls into named cards and sensible Inspector categories. Keep important controls discoverable without requiring a long scroll.
- Make responsive layouts deliberate: retain usable controls at tablet and narrow widths, and avoid clipped toolbars, overlapping panels, or a hidden primary action.

## Visual style and icons

- Prefer dark neutral charcoal backgrounds, subtly differentiated cards, rounded corners, filled pill controls, and restrained separators. Reserve brighter colors for selection, status, and meaningful data.
- Use flat icons with consistent weight and optical size. Prefer the locally bundled Font Awesome Free solid font already used by Vehicle Lab; follow its bundled license and keep icon assets self-hosted. Do not hotlink icon/font CDNs or introduce hand-drawn Unicode symbols as substitutes for a coherent icon set.
- Use semantic, accessible labels and tooltips for icon-only actions. Keep focus indicators visible and provide useful accessible names for controls.
- Avoid decorative effects that compete with the visualization: no glowing UI accents, and no flat 2D grid laid over a 3D Stage. A real 3D ground grid may be used when it helps orient the scene. Keep graph-specific grids inside graph editors only.

## Controls and state

- Make controls operate on real application state rather than serving as static mock UI. Pair numeric sliders with editable numeric values when precision matters, and always show units, range, and meaningful defaults.
- For curves and other multi-mode tools, put options next to the edited content, give each choice a clear name, and apply the choice to rendering/evaluation—not only to a label. Persist per-asset choices where appropriate and include them in exports.
- Keep selection and editing contextual. Selecting an Outliner component should update the Inspector and, where supported, identify that component in the Stage.
- Component creation should produce a real, named child in the scene/component model. Support appropriate parent or mount selection, editable properties, visibility, selection, removal, and persistence; do not make an Add button that only appends a decorative row.
- Mark read-only source assets with a lock affordance. Show warning/caution status when a source or dependency is missing or invalid, and a hidden/visibility status when an item is disabled. Tooltips should explain each status.
- Simulation controls must run a meaningful preview using the relevant model/parameters, expose run/pause/reset behavior as appropriate, and present useful live readouts. Label simplified previews honestly; do not imply they execute arbitrary code or a production solver when they do not.

## Debugging and visualization

- Keep debug channels selectable and named. Their graphic should be understandable at a glance and use consistent semantic colors.
- Place annotations, vectors, springs, labels, and other diagnostics where they remain visible outside/alongside occluding geometry when possible. Avoid drawing every diagnostic inside the asset where it cannot be inspected. Use exterior callouts or deliberate x-ray/overlay depth behavior only when it improves legibility without obscuring the asset.
- A debug visualization must not silently modify the authored source asset. Distinguish inspection overlays from editable geometry and simulation state.

## Implementation and verification

- Follow the host studio's existing module boundaries, styles, naming, asset paths, and responsive conventions. Share behavior where practical rather than duplicating entire editor implementations.
- Keep assets local and document licenses. Avoid unneeded dependencies and large generated artifacts.
- Test JavaScript/module syntax, static validation, DOM IDs and handlers, asset/network paths, and the actual preview where available. Add or update a focused validation script for important interactions.
- Bind preview servers to `0.0.0.0` and use relative browser-facing asset URLs so the hosted preview works outside the sandbox.
- Update the relevant suite and studio README files when adding an editor, UI pattern, or launch/change workflow.
