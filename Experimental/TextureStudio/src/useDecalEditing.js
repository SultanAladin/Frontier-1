import { AUTHORING_UV_SET, texturePart } from "./textureSurfaceModel.js";
import { useEffect, useRef, useState } from "react";
import {
  createStampDocument,
  normalizeStampPreset,
  readStampPresets,
} from "./stampDocument.js";
import { renderStamp } from "./stampRaster.js";
import {
  addTextureDecal,
  patchTextureDecal,
  removeTextureDecal,
  replaceTextureDecalStamp,
  isTextureLayerLocked,
  isTextureLayerVisible,
  isTexturePartLocked,
} from "./textureDocument.js";
export default function useDecalEditing(
  history,
  {
    initialStamp,
    onStampConsumed,
    onEditStamp,
    previewLayerId,
    onDeactivateGradient,
    space = "object-sphere",
    placementAvailable = true,
    onRequestSpace,
  },
) {
  const { doc, selection } = history;
  const [presets, setPresets] = useState(readStampPresets),
    [fallback, setFallback] = useState(null),
    [presetId, setPresetId] = useState(initialStamp?.id || null);
  const [componentId, setComponentId] = useState(null),
    [operation, setOperation] = useState({
      active: false,
      mode: "brush",
      gizmo: "translate",
    });
  const [brushes, setBrushes] = useState({
    "object-sphere": {
      size: [0.75, 0.75],
      depth: 0.35,
      opacity: 1,
      roll: 0,
      asMask: false,
    },
    uv: { size: [0.18, 0.18], depth: 0.35, opacity: 1, roll: 0, asMask: false },
  });
  const brush = {
    ...brushes[space],
    space,
    ...(space === "uv" ? { uvSet: AUTHORING_UV_SET } : {}),
  };
  const [draft, setDraft] = useState(null),
    draftRef = useRef(null);
  draftRef.current = draft;
  const incoming = useRef(initialStamp),
    requestedSelection = useRef(null);
  const available = [
    ...presets,
    ...(fallback && !presets.length ? [fallback] : []),
  ];
  const preset =
    available.find((s) => s.id === presetId) ||
    available[0] ||
    incoming.current;
  const owner = doc.layers.find((l) => l.id === selection.id),
    component = owner?.decals.find((d) => d.id === componentId);
  const locked =
    !owner || owner.kind === "folder" || isTextureLayerLocked(doc, owner.id);
  const partLocked =
    !!component?.target && isTexturePartLocked(doc, component.target.partId);
  const active =
    operation.active &&
    (!partLocked || operation.mode === "brush") &&
    placementAvailable &&
    (operation.mode === "brush" || !component || component.space === space) &&
    !locked &&
    !selection.mask &&
    isTextureLayerVisible(doc, owner.id) &&
    !!preset &&
    (!component?.locked || operation.mode === "brush");
  useEffect(() => {
    let cancelled = false;
    const change = () => setPresets(readStampPresets());
    window.addEventListener("alloy-stamp-library-change", change);
    const project = createStampDocument();
    renderStamp(project)
      .then(({ raster }) => {
        if (!cancelled) setFallback(normalizeStampPreset({ project, raster }));
      })
      .catch((e) => {
        if (!cancelled) history.setError(e.message);
      });
    return () => {
      cancelled = true;
      window.removeEventListener("alloy-stamp-library-change", change);
    };
  }, []);
  useEffect(() => {
    if (!initialStamp) return;
    incoming.current = initialStamp;
    setPresetId(initialStamp.id);
    setPresets((old) => [
      initialStamp,
      ...old.filter((p) => p.id !== initialStamp.id),
    ]);
    setOperation((old) => ({ ...old, active: true, mode: "brush" }));
    onDeactivateGradient();
    onStampConsumed?.();
  }, [initialStamp?.id]);
  useEffect(() => {
    if (requestedSelection.current?.layerId === selection.id) {
      const request = requestedSelection.current;
      setComponentId(request.id);
      requestedSelection.current = null;
      setOperation((old) => ({
        ...old,
        active: true,
        mode: request.mode || "transform",
      }));
      return;
    }
    if (incoming.current) {
      incoming.current = null;
      return;
    }
    setComponentId(null);
    setOperation((old) => ({ ...old, active: false }));
  }, [selection.id, selection.mask]);
  useEffect(() => {
    if (componentId && !component) {
      setComponentId(null);
      setOperation((old) => ({
        ...old,
        active: old.mode === "brush" && old.active,
      }));
    }
  }, [componentId, !!component]);
  function arm(mode) {
    if (locked || !preset || !placementAvailable) return;
    if (mode !== "brush" && component) onRequestSpace?.(component.space);
    onDeactivateGradient();
    setOperation((old) => ({
      ...old,
      active: true,
      mode: mode === "transform" && !component ? "place" : mode,
    }));
  }
  function patchId(
    id,
    changes,
    key = null,
    expected = null,
    label = "Edit decal component",
    layerId = selection.id,
  ) {
    const current = history.docRef.current,
      layer = current.layers.find((l) => l.id === layerId),
      decal = layer?.decals.find((d) => d.id === id);
    if (!decal || (expected && JSON.stringify(decal) !== expected))
      return false;
    try {
      return history.commit(patchTextureDecal(current, layerId, id, changes), {
        key: key ? `decal-${id}-${key}` : null,
        label,
      });
    } catch (e) {
      history.setError(e.message);
      return false;
    }
  }
  useEffect(() => {
    const stored = doc.layers
      .find((l) => l.id === draft?.layerId)
      ?.decals.find((d) => d.id === draft?.id);
    if (
      draft &&
      (!active || JSON.stringify(stored) !== draft.expected || space !== "uv")
    ) {
      draftRef.current = null;
      setDraft(null);
    }
  }, [doc, active, space]);
  function previewTransform(patch, expected) {
    if (
      !active ||
      component?.space !== "uv" ||
      component.locked ||
      JSON.stringify(component) !== expected
    )
      return;
    const value = { id: component.id, layerId: selection.id, patch, expected };
    draftRef.current = value;
    setDraft(value);
  }
  function endTransform(cancelled) {
    const value = draftRef.current;
    draftRef.current = null;
    setDraft(null);
    if (value && !cancelled)
      return patchId(
        value.id,
        value.patch,
        null,
        value.expected,
        "Transform UV decal",
        value.layerId,
      );
    return false;
  }
  function stamp(hit) {
    const current = history.docRef.current,
      layer = current.layers.find((l) => l.id === selection.id);
    if (
      !active ||
      !layer ||
      isTextureLayerLocked(current, layer.id) ||
      !isTextureLayerVisible(current, layer.id)
    )
      return;
    if (operation.mode === "place" && component) {
      if ((hit.space || "object-sphere") !== component.space) return;
      if (
        patchId(
          component.id,
          { ...hit, roll: component.roll },
          null,
          JSON.stringify(component),
          "Re-anchor surface decal",
        )
      )
        setOperation((old) => ({ ...old, mode: "transform" }));
    } else {
      if (!brush.asMask && !preset.project.channels.length) {
        history.setError(
          "Enable at least one stamp channel, or use the stamp as a mask.",
        );
        return;
      }
      if (brush.opacity === 0) return;
      try {
        const result = addTextureDecal(current, selection.id, preset, {
          ...brush,
          ...hit,
        });
        if (
          result.id &&
          history.commit(result.doc, {
            label: brush.asMask ? "Stamp surface mask" : "Stamp surface decal",
          })
        ) {
          setComponentId(result.id);
          if (operation.mode !== "brush")
            setOperation((old) => ({ ...old, mode: "transform" }));
        }
      } catch (e) {
        history.setError(e.message);
      }
    }
  }
  function dropStamp(id, hit) {
    const source = available.find((s) => s.id === id),
      current = history.docRef.current,
      target = current.layers.find((l) => l.id === selection.id);
    if (!source || !hit?.target) {
      history.setError("Choose an existing stamp and a mesh surface.");
      return false;
    }
    if (
      !target ||
      target.kind === "folder" ||
      selection.mask ||
      isTextureLayerLocked(current, target.id) ||
      !isTextureLayerVisible(current, target.id)
    ) {
      history.setError(
        "Select a visible, unlocked surface or decal layer before dropping a stamp.",
      );
      return false;
    }
    if (!brush.asMask && !source.project.channels.length) {
      history.setError("Enable a stamp channel, or use the stamp as a mask.");
      return false;
    }
    if (!brush.opacity) return false;
    try {
      const result = addTextureDecal(current, target.id, source, {
        ...brush,
        ...hit,
      });
      if (
        !result.id ||
        !history.commit(result.doc, {
          label: hit.space === "uv" ? "Drop UV decal" : "Drop surface decal",
        })
      )
        return false;
      onDeactivateGradient();
      setPresetId(id);
      setComponentId(result.id);
      setOperation((old) => ({ ...old, active: true, mode: "transform" }));
      return true;
    } catch (error) {
      history.setError(error.message);
      return false;
    }
  }
  const context = {
    id: componentId,
    preset,
    presets: available,
    active,
    mode: operation.mode,
    gizmo: operation.gizmo,
    brush,
    space,
    placementAvailable,
    partLocked,
    onArm: arm,
    onNavigate: () => {
      endTransform(true);
      setOperation((old) => ({ ...old, active: false }));
    },
    onBrush: (changes) =>
      setBrushes((old) => ({ ...old, [space]: { ...old[space], ...changes } })),
    onGizmo: (gizmo) => {
      if (component) onRequestSpace?.(component.space);
      onDeactivateGradient();
      setOperation((old) => ({
        ...old,
        active: true,
        mode: "transform",
        gizmo,
      }));
    },
    onPreset: setPresetId,
    onPresetDrop: (id, layerId = selection.id) => {
      if (!available.some((s) => s.id === id)) return;
      const target = history.docRef.current.layers.find(
        (l) => l.id === layerId,
      );
      if (
        !target ||
        target.kind === "folder" ||
        isTextureLayerLocked(history.docRef.current, layerId)
      )
        return;
      onDeactivateGradient();
      setPresetId(id);
      setComponentId(null);
      if (layerId !== selection.id) {
        requestedSelection.current = { layerId, id: null, mode: "brush" };
        history.select(layerId);
      }
      setOperation((old) => ({ ...old, active: true, mode: "brush" }));
    },
    onSelect: (id, layerId = selection.id) => {
      const chosen = history.docRef.current.layers
        .find((l) => l.id === layerId)
        ?.decals.find((d) => d.id === id);
      if (chosen) onRequestSpace?.(chosen.space);
      if (layerId !== selection.id) {
        requestedSelection.current = { layerId, id };
        history.select(layerId);
      }
      setComponentId(id);
      onDeactivateGradient();
      setOperation((old) => ({ ...old, active: true, mode: "transform" }));
    },
    onPatch: (changes, key) => patchId(componentId, changes, key),
    onPatchId: patchId,
    onDuplicate: (id) => {
      const current = history.docRef.current,
        layer = current.layers.find((l) => l.id === selection.id),
        decal = layer?.decals.find((d) => d.id === id),
        source = current.stamps.find((s) => s.id === decal?.stampId);
      if (!decal || decal.locked || !source) return;
      try {
        const result = addTextureDecal(current, selection.id, source, {
          ...decal,
          id: undefined,
          ...(decal.space === "uv"
            ? {
                uvPosition: [
                  Math.min(
                    (texturePart(decal.target?.partId)?.u ?? 9) + 0.99999,
                    decal.uvPosition[0] + 0.01,
                  ),
                  decal.uvPosition[1],
                ],
              }
            : {
                position: decal.position.map((v, i) =>
                  i === 0 ? v + 0.05 : v,
                ),
              }),
        });
        if (history.commit(result.doc, { label: "Duplicate decal component" }))
          setComponentId(result.id);
      } catch (e) {
        history.setError(e.message);
      }
    },
    onRemove: (id) => {
      try {
        if (
          history.commit(
            removeTextureDecal(history.docRef.current, selection.id, id),
            { label: "Remove decal component" },
          )
        ) {
          setComponentId(null);
          setOperation((old) => ({ ...old, active: false }));
        }
      } catch (e) {
        history.setError(e.message);
      }
    },
    onReplace: (id) => {
      if (!preset) return;
      try {
        history.commit(
          replaceTextureDecalStamp(
            history.docRef.current,
            selection.id,
            id,
            preset,
          ),
          { label: "Replace decal artwork & targets" },
        );
      } catch (e) {
        history.setError(e.message);
      }
    },
    onEditStamp: (project) => onEditStamp?.(project),
  };
  const previewDocument =
    draft &&
    doc.layers
      .find((l) => l.id === draft.layerId)
      ?.decals.some(
        (d) => d.id === draft.id && JSON.stringify(d) === draft.expected,
      )
      ? {
          ...doc,
          layers: doc.layers.map((l) =>
            l.id === draft.layerId
              ? {
                  ...l,
                  decals: l.decals.map((d) =>
                    d.id === draft.id ? { ...d, ...draft.patch } : d,
                  ),
                }
              : l,
          ),
        }
      : doc;
  return {
    context,
    document: previewDocument,
    dropStamp,
    deactivate: context.onNavigate,
    clearSelection: () => {
      setComponentId(null);
      context.onNavigate();
    },
    editor: {
      active,
      mode: operation.mode,
      gizmo: operation.gizmo,
      layerId: selection.id,
      id: componentId,
      previewLayerId,
      surfaceTarget: owner?.surfaceTarget,
      preset:
        operation.mode === "place" && component
          ? doc.stamps.find((s) => s.id === component.stampId)
          : preset,
      brush: operation.mode === "place" && component ? component : brush,
      onStamp: stamp,
      onPreviewTransform: previewTransform,
      onTransformEnd: endTransform,
      onSelect: context.onSelect,
      onTransform: (patch, expected) =>
        patchId(componentId, patch, null, expected, "Transform surface decal"),
      onError: history.setError,
    },
  };
}
