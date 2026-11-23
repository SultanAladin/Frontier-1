//============================================================================================================================================
//                                                             PROJECTBROWSER.JSX
//============================================================================================================================================
// 🧩 Project Browser (Project Zero specification §14, Follow-up 48). Default Project only, no sample scenes. "+ New Project" makes a blank
// scene (ground plane and a clear sky) and opens it in Frontier; there are no UV, Texture or other workspace launch buttons. Open a
// .projectspace file or type a path; search; pick a saved version. Projects and their versions live in this browser only.

import React, { useEffect, useMemo, useRef, useState } from "react";

export const DefaultProjectToken = "default";
export const ProjectStorageKey = "Frontier.ProjectZeroProjects.v1";
export const MaximumProjectVersions = 3;

export function RestoreProjects() {
  try {
    const Saved = JSON.parse(localStorage.getItem(ProjectStorageKey)) || {};
    const Projects = Array.isArray(Saved.Projects) ? Saved.Projects.filter((Each) => Each && typeof Each.Token === "string") : [];
    if (!Projects.some((Each) => Each.Token === DefaultProjectToken))
      Projects.unshift({ Token: DefaultProjectToken, Name: "Default Project", Location: "Projects/Default Project/Default.projectspace", Created: null, Versions: [] });
    return { Active: typeof Saved.Active === "string" ? Saved.Active : DefaultProjectToken, Projects };
  } catch {
    return { Active: DefaultProjectToken, Projects: [{ Token: DefaultProjectToken, Name: "Default Project", Location: "Projects/Default Project/Default.projectspace", Created: null, Versions: [] }] };
  }
}
export function StoreProjects(Catalogue) {
  try {
    localStorage.setItem(ProjectStorageKey, JSON.stringify(Catalogue));
    return true;
  } catch {
    // ⚠️ Storage quota: drop the oldest versions and retry once.
    try {
      localStorage.setItem(ProjectStorageKey, JSON.stringify({ ...Catalogue, Projects: Catalogue.Projects.map((Each) => ({ ...Each, Versions: Each.Versions.slice(-1) })) }));
      return true;
    } catch {
      return false;
    }
  }
}

// Blank scene rows: a ground plane and a clear sky (atmosphere and sun; no clouds, fog or sample objects).
export const BlankSceneRows = [
  { Id: "scene", Name: "Scene", Panel: "group", Icon: "folder-scene", Parent: null, Description: "Scene collection" },
  { Id: "ground-plane", Name: "Ground Plane", Panel: "geometry", Icon: "editor-cube", Parent: "scene", Description: "Ground plane · 100 × 100 m", Preview: true },
  { Id: "cameras", Name: "Cameras", Panel: "group", Icon: "camera", Parent: null, Description: "Camera collection" },
  { Id: "world", Name: "World", Panel: "group", Icon: "folder-world", Parent: null, Description: "Environment" },
  { Id: "atmosphere", Name: "Atmosphere", Panel: "atmosphere", Icon: "sky-scattering", Parent: "world", Description: "Procedural sky" },
  { Id: "sun", Name: "Sun", Panel: "sun", Icon: "sun", Parent: "world", Description: "Directional light" },
];
export const BlankSceneValues = { "ground-plane": { Scale: [100, 0.02, 100], Position: [0, 0, 0] } };

const Ago = (Time) => (!Time ? "Built in" : new Date(Time).toLocaleString("en-GB", { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" }));

export default function ProjectBrowser({ Catalogue, Open, Create, OpenFile, OpenPath, Close }) {
  const [Query, Search] = useState("");
  const [Picked, Pick] = useState(Catalogue.Active);
  const [Creating, SetCreating] = useState(false);
  const [Name, Rename] = useState("Untitled Project");
  const [Location, Relocate] = useState("Projects/");
  const [Path, SetPath] = useState("");
  const [Version, SetVersion] = useState(-1);
  const FileInput = useRef(null);
  const Dialog = useRef(null);
  useEffect(() => Dialog.current?.focus(), []);
  const Shown = useMemo(
    () => Catalogue.Projects.filter((Each) => (Each.Name + " " + Each.Location).toLowerCase().includes(Query.trim().toLowerCase())),
    [Catalogue, Query],
  );
  const Selected = Catalogue.Projects.find((Each) => Each.Token === Picked);
  const Versions = Selected?.Versions || [];
  const Clean = Name.replace(/[\x00-\x1f\x7f<>:"/\\|?*]/g, "").trim();
  return (
    <div className="project-browser-backdrop" onPointerDown={(Event) => Event.target === Event.currentTarget && Close()}>
      <section
        ref={Dialog}
        tabIndex={-1}
        className="project-browser"
        role="dialog"
        aria-modal="true"
        aria-label="Project Browser"
        onKeyDown={(Event) => {
          if (Event.key === "Escape") {
            Event.stopPropagation();
            Close();
          }
        }}
      >
        <header>
          <h2>Project Browser</h2>
          <button type="button" aria-label="Close Project Browser" onClick={Close}>
            ×
          </button>
        </header>
        <div className="pb-tools">
          <input type="search" aria-label="Search projects" placeholder="Search projects" value={Query} onChange={(Event) => Search(Event.target.value)} />
          <button type="button" onClick={() => FileInput.current?.click()}>
            Open .projectspace…
          </button>
          <input
            ref={FileInput}
            type="file"
            accept=".projectspace,application/json"
            hidden
            aria-label="Open projectspace file"
            onChange={(Event) => {
              const File_ = Event.target.files?.[0];
              if (File_) OpenFile(File_);
              Event.target.value = "";
            }}
          />
        </div>
        <div className="pb-tools">
          <input type="text" aria-label="Project path" placeholder="Path to a .projectspace file" value={Path} onChange={(Event) => SetPath(Event.target.value)} />
          <button type="button" disabled={!Path.trim()} onClick={() => OpenPath(Path.trim())}>
            Open path
          </button>
        </div>
        <div className="pb-grid">
          {Shown.map((Project) => (
            <button
              key={Project.Token}
              type="button"
              className="pb-card"
              aria-pressed={Picked === Project.Token}
              data-project={Project.Token}
              onClick={() => {
                Pick(Project.Token);
                SetVersion(-1);
                SetCreating(false);
              }}
              onDoubleClick={() => Open(Project, -1)}
            >
              <div className="pb-thumb" aria-hidden="true" />
              <b>{Project.Name}</b>
              <small>{Project.Location}</small>
              <small>
                {Project.Token === Catalogue.Active ? "Open now · " : ""}
                {Ago(Project.Created)}
              </small>
            </button>
          ))}
          {!Query && (
            <button type="button" className="pb-card pb-new" aria-pressed={Creating} onClick={() => SetCreating(true)}>
              + New Project
            </button>
          )}
        </div>
        {Creating ? (
          <div className="pb-create" aria-label="New project">
            <label>
              Project name
              <input aria-label="Project name" value={Name} onChange={(Event) => Rename(Event.target.value)} />
            </label>
            <label>
              Location
              <input aria-label="Project location" value={Location} onChange={(Event) => Relocate(Event.target.value)} />
            </label>
            <button type="button" className="pb-primary" disabled={!Clean} onClick={() => Create(Clean, Location.trim() || "Projects/")}>
              Create &amp; Open
            </button>
          </div>
        ) : (
          Selected && (
            <div className="pb-create" aria-label="Open project">
              <label>
                Version
                <select aria-label="Project version" value={Version} onChange={(Event) => SetVersion(+Event.target.value)}>
                  <option value={-1}>Latest</option>
                  {Versions.map((Each, Index) => (
                    <option key={Index} value={Index}>
                      {Each.Label}
                    </option>
                  ))}
                </select>
              </label>
              <label>
                Location
                <input aria-label="Selected project location" value={Selected.Location} readOnly />
              </label>
              <button type="button" className="pb-primary" onClick={() => Open(Selected, Version)}>
                Open
              </button>
            </div>
          )
        )}
        <p className="pb-note">
          New projects open in Frontier with a ground plane and a clear sky. Projects and versions are kept in this browser; a typed path is recorded for the
          native hand-off, which is not connected.
        </p>
      </section>
    </div>
  );
}
