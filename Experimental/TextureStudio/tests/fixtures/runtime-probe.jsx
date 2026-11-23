// Lazy-loaded regression fixture: checks the hook dispatcher after the studio
// has already loaded React DOM and switched workspaces. Not imported by the app.
import React, { useState } from "react";
import { createRoot } from "react-dom/client";
import * as THREE from "three";

export const runtimeThree = THREE;
let root;
function HookProbe() {
  const [state, setState] = useState("Runtime hook ready");
  return (
    <button
      data-testid="runtime-hook-probe"
      onClick={() => setState("Runtime hook updated")}
    >
      {state}
    </button>
  );
}
export function mountRuntimeProbe(host) {
  root = createRoot(host);
  root.render(<HookProbe />);
}
export function unmountRuntimeProbe() {
  root?.unmount();
  root = null;
}
