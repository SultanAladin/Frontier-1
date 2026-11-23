//============================================================================================================================================
//                                                        COMPONENTVIEWERPANEL.JSX
//============================================================================================================================================
// 🧩 Hosts the Component Viewer (Experimental/ComponentViewer, ported from SultanAladin/Frontier-1 unchanged) over the viewport. The
// viewer keeps its own page, scripts and storage; Project Zero only hosts it, so the two never share storage. Esc or × closes it, and
// it can also open in its own tab.

import React, { useEffect, useState } from "react";
import "./ComponentViewerPanel.css";

// Serve.py hands the editor out at "/" with the repository as the file root; any other host serves it from its own folder.
export function ComponentViewerAddress(Location = window.location) {
  const AtRoot = Location.protocol !== "file:" && /^\/(index\.html)?$/.test(Location.pathname);
  return AtRoot ? "/Experimental/ComponentViewer/index.html" : "../ComponentViewer/index.html";
}

export default function ComponentViewerPanel({ Close, Icon }) {
    const [Loaded, SetLoaded] = useState(false);
  useEffect(() => {
    const Leave = (Event) => {
      if (Event.key === "Escape") {
        Event.stopPropagation();
        Close();
      }
    };
    window.addEventListener("keydown", Leave, true);
    return () => window.removeEventListener("keydown", Leave, true);
  }, [Close]);
  return (
    <section className="cv-host" role="dialog" aria-label="Component Viewer">
      <header className="cv-host-bar">
        <span className="cv-host-title">
          {Icon && <Icon Name="component-viewer" Size={18} />}
          <b>Component Viewer</b>
          <small>{Loaded ? "Inspect, compile and bake components" : "Loading…"}</small>
        </span>
        <span className="cv-host-actions">
          <button type="button" onClick={() => window.open(ComponentViewerAddress(), "_blank", "noopener")}>
            Open in new tab
          </button>
          <button type="button" className="cv-host-close" aria-label="Close Component Viewer" onClick={Close}>
            ×
          </button>
        </span>
      </header>
      <iframe className="cv-host-view" title="Component Viewer" src={ComponentViewerAddress()} onLoad={() => SetLoaded(true)} />
    </section>
  );
}
