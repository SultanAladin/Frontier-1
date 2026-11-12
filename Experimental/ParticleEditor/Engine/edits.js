// One place an edit passes through, so that something other than the GPU can notice it.
//
// 🔴 WHY THIS EXISTS. As merged, every inspector row in app.js wrote straight into `sys.p` from its own
//    closure — thirty-two separate `(v) => (p.rate = v)` arrows. Nothing could observe an edit, so nothing
//    could react to one: no undo, no dirty marker, no save prompt, and no way to tell a host editor that
//    the author just moved a slider. The scene handoff had to diff a snapshot on a timer instead.
//
// 💡 THE FUNNEL IS AT THE WIDGET, NOT AT THE PARAMETER. The obvious fix is `setParam(sys, key, value)`,
//    called by all thirty-two closures. That means editing thirty-two closures, and the thirty-third one
//    written next week will forget. Every one of those closures is already reached through six row
//    helpers — rangeRow, vecRow, colorRow, selectRow, checkRow and the action buttons — so the funnel goes
//    there instead: six call sites, and a new row is funnelled by construction because the only way to
//    draw a row is to call one of the six.
//
// The cost of choosing the widget layer, stated plainly: an edit made in code rather than by a person is
//    not announced. That is the right side to err on — this exists to report authoring, and a programmatic
//    write (a preset being applied, a host message being admitted) is the caller's own business to report.
// 📝 Converted from an IIFE to an ES module; the body keeps its two-space indent so that `git blame`
//    still points at whoever wrote each line rather than at the conversion.
import { PE } from "./pe.js";

  const Watchers = [];
  let Subject = null;     // the system whose rows are currently being built
  let Depth = 0;          // > 0 while a programmatic edit is running, which is not authoring

  // 📦 Name the system that subsequent rows belong to. renderInspector calls this once, at the top.
  //    Rows capture their system in a closure; the funnel cannot see that closure, so it is told.
  function For(sys) {
    const Was = Subject;
    Subject = sys || null;
    return Was;
  }

  // 📦 Hear about edits. Returns the function that stops the hearing — a subscriber that cannot
  //    unsubscribe is a leak the first time an editor is opened twice.
  function Subscribe(Watcher) {
    if (typeof Watcher !== "function") return () => {};
    Watchers.push(Watcher);
    return () => {
      const At = Watchers.indexOf(Watcher);
      if (At >= 0) Watchers.splice(At, 1);
    };
  }

  // 📦 Run something without announcing what it changes — applying a preset, or admitting a host's
  //    message, where the caller already knows and the echo would come straight back.
  function Quietly(Work) {
    Depth++;
    try { return Work(); }
    finally { Depth--; }
  }

  // 📦 An author changed something. `What` is the row's own label, which is what a person would call it;
  //    it is for logs and undo descriptions, not for addressing the parameter.
  function Announce(What, Value) {
    if (Depth > 0) return 0;
    const Edit = { System: Subject, What: String(What ?? ""), Value };
    let Heard = 0;
    for (const Watcher of Watchers.slice()) {
      // One throwing watcher must not stop the slider from moving, or break the watchers behind it.
      try { Watcher(Edit); Heard++; }
      catch (Trouble) { console.error("edit watcher failed", Trouble); }
    }
    return Heard;
  }

  // 📦 Wrap a row's setter so that using the row announces it. This is what the six helpers call.
  function Through(What, Set) {
    return (Value) => {
      const Answer = Set(Value);
      Announce(What, Value);
      return Answer;
    };
  }

  PE.Edits = { For, Subscribe, Quietly, Announce, Through, Watchers };
