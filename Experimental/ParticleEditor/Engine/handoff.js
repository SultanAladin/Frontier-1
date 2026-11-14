// Scene handoff for the Particle Editor: the one sentence in each direction that lets a host editor open
// this page for an emitter and get the authored system back.
//
// 🔴 WHY THIS EXISTS. As merged, the Particle Editor had no persistence of any kind — no save, no load, no
//    export. Fifty-two presets can be tuned in it and nothing can leave. That is survivable for a
//    standalone toy and fatal for an editor the gas emitter card opens, so this adds the smallest thing
//    that makes the trip possible: postMessage in, postMessage out, in the same shape GasPanel.jsx already
//    speaks to the Fluid simulator ("gas-scene" / "gas-scene-changed").
//
// 📝 HOW A CHANGE IS NOTICED. js/edits.js funnels every inspector row through one announcement, so this
//    subscribes rather than polling. The announcement says only that something was edited; this still
//    re-describes the scene and compares, because the row's label is not the parameter's name and a
//    description that is merely different from the last one is the only claim worth posting.
//
//    A slow safety net remains behind the subscription, at a much longer interval, for the writes that do
//    not come from a row at all — a preset applied in code, a system removed. Catching those within a
//    couple of seconds is enough; they are not what an author is dragging.
// 📝 Converted from an IIFE to an ES module; the body keeps its two-space indent so that `git blame`
//    still points at whoever wrote each line rather than at the conversion.
import { PE } from "./pe.js";

  const SETTLE_MS = 90;    // [ms] edits are coalesced for this long, so dragging a slider posts once
  const SWEEP_MS = 2000;   // [ms] the safety net, for changes that never passed through a row

  // Values a host is allowed to set. Anything else in an arriving message is ignored rather than merged:
  //    a message is untrusted input, and Object.assign over sys.p would let a sender invent fields that
  //    the uniform packer then reads as NaN.
  function Writable(p) {
    const Out = {};
    for (const [Key, Value] of Object.entries(p)) {
      if (typeof Value === "number" || typeof Value === "boolean" || typeof Value === "string") Out[Key] = Value;
      else if (Array.isArray(Value) && Value.every((One) => typeof One === "number")) Out[Key] = Value.slice();
    }
    return Out;
  }

  // 📦 What this page would tell a host about the system it is editing.
  //    Returns null when nothing is selected, which a caller must treat as "say nothing", not as "empty".
  function Describe(state) {
    if (!state || state.selection?.type !== "system") return null;
    const sys = (state.systems || []).find((One) => One.id === state.selection.id);
    if (!sys) return null;
    return {
      Frontier: "particle-system-changed",
      Preset: sys.presetId,
      Name: sys.name,
      Settings: Writable(sys.p),
    };
  }

  // 📦 Apply a host's message to a system's parameters.
  //    in    p         the system's live parameters, written in place
  //    in    Settings  an arriving object, untrusted
  //    out   number    how many fields were taken; a field of the wrong shape is skipped, not coerced
  function Admit(p, Settings) {
    if (!p || !Settings || typeof Settings !== "object") return 0;
    let Taken = 0;
    for (const [Key, Value] of Object.entries(Settings)) {
      if (!Object.hasOwn(p, Key)) continue;                       // no inventing fields
      const Was = p[Key];
      if (Array.isArray(Was)) {
        if (!Array.isArray(Value) || Value.length !== Was.length) continue;
        if (!Value.every((One) => Number.isFinite(One))) continue;
        p[Key] = Value.slice();
      } else if (typeof Was === "number") {
        if (!Number.isFinite(Value)) continue;
        p[Key] = Value;
      } else if (typeof Was === "boolean") {
        if (typeof Value !== "boolean") continue;
        p[Key] = Value;
      } else if (typeof Was === "string") {
        if (typeof Value !== "string") continue;
        p[Key] = Value;
      } else continue;
      Taken++;
    }
    return Taken;
  }

  // Two descriptions are the same scene when their preset, name and every setting agree.
  function Same(A, B) {
    return A === B || (!!A && !!B && JSON.stringify(A) === JSON.stringify(B));
  }

  // 📦 Wire the page to whatever window opened it.
  //    in    state    the application state
  //    in    Host     { Open(presetId, name), Rebuild(sys), Refresh() } — app.js's own verbs
  function Install(state, Host) {
    if (typeof window === "undefined" || !window.parent || window.parent === window) return () => {};
    let Last = null;

    const Receive = (Event) => {
      const Message = Event.data;
      if (!Message || Message.Frontier !== "particle-system") return;
      // 🔴 Quietly: admitting the host's own message must not announce an edit, or the subscription
      //    above posts it straight back and the two windows talk to each other forever.
      PE.Edits.Quietly(() => {
        const sys = Host.Open(Message.Preset, Message.Name);
        if (!sys) return;
        if (Admit(sys.p, Message.Settings) > 0) Host.Rebuild(sys);
        Host.Refresh();
      });
      Last = Describe(state);
    };
    window.addEventListener("message", Receive);

    // Post only when the scene actually reads differently, however the change arrived.
    const Post = () => {
      const Now = Describe(state);
      if (!Now || Same(Now, Last)) return false;
      Last = Now;
      window.parent.postMessage(Now, "*");
      return true;
    };

    // Dragging a slider announces on every input event. Coalescing to one post per settle keeps a drag
    //    from becoming a hundred messages, while still landing well inside a person's reaction time.
    let Settling = 0;
    const Unsubscribe = PE.Edits.Subscribe(() => {
      if (Settling) return;
      Settling = window.setTimeout(() => { Settling = 0; Post(); }, SETTLE_MS);
    });

    const Sweep = window.setInterval(Post, SWEEP_MS);

    window.parent.postMessage({ Frontier: "particle-editor-ready" }, "*");
    return () => {
      window.removeEventListener("message", Receive);
      window.clearInterval(Sweep);
      if (Settling) window.clearTimeout(Settling);
      Unsubscribe();
    };
  }

  PE.Handoff = { Describe, Admit, Writable, Same, Install, SETTLE_MS, SWEEP_MS };
