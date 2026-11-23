//============================================================================================================================================
//                                                               DEBUGVIEWS.JS
//============================================================================================================================================
// 🧩 Viewport debug views. 00–16 follow the Project Zero specification (Follow-up 48); the views earlier drafts listed follow with a 📝
// note because the specification does not include them. Selections only: the HTML preview has no native render buffers.

export const DebugViews = [
  { Code: "00", Name: "Lit", Shows: "ACES final" },
  { Code: "01", Name: "Depth", Shows: "pow(1 − d / 11 m, 1.8)" },
  { Code: "02", Name: "Albedo", Shows: "Raw diffuse colour" },
  { Code: "03", Name: "Shading normal", Shows: "N × 0.5 + 0.5" },
  { Code: "04", Name: "Roughness", Shows: "SpecParams.x" },
  { Code: "05", Name: "Metalness", Shows: "SpecParams.y" },
  { Code: "06", Name: "Cluster ID", Shows: "Hash colour per slot" },
  { Code: "07", Name: "Geometric normal", Shows: "Triangle faces" },
  { Code: "08", Name: "Sun shadow", Shows: "Shadow pass + filter" },
  { Code: "09", Name: "AO (filtered)", Shows: "Cone AO; GDF, engines 1–2" },
  { Code: "10", Name: "Direct lighting", Shows: "ACES direct HDR" },
  { Code: "11", Name: "Specular", Shows: "BVH4 / Sky" },
  { Code: "12", Name: "SDF distance", Shows: "Red < 0 · green 0 · blue ≥ 10 voxels" },
  { Code: "13", Name: "SDF with materials", Shows: "Albedo × (0.18 + 0.82 wrap N·L)" },
  { Code: "14", Name: "SDF normal", Shows: "ε 0.75 voxel" },
  { Code: "15", Name: "SDF AO raw", Shows: "Unfiltered; compare with 09" },
  { Code: "16", Name: "SDF cascade", Shows: "Two 64³ cascades: green fine · orange coarse · red outside; cluster density and clusters" },
  // 📝 Earlier editor views, not in the specification. Kept for the user to decide.
  ...[
    "Visibility ID",
    "Motion Vectors",
    "HiZ (level 3)",
    "Reservoir M",
    "Reservoir W",
    "Reservoir Age",
    "Patch Tiles",
    "Tiles + Wireframe",
  ].map((Name) => ({ Code: "", Name, Shows: "Earlier editor view · not in the Project Zero specification", SpecificationNote: true })),
];

export const DebugViewLabel = (View) => (!View ? "" : View.SpecificationNote ? `📝 ${View.Name}` : `${View.Code} · ${View.Name}`);
