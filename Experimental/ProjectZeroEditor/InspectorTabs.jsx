//============================================================================================================================================
//                                                              INSPECTORTABS.JSX
//============================================================================================================================================
// 🧩 Lettered inspector pages [A | B | C …] for the environment and camera inspectors. The cards come first; the pages are picked from
// the Orbit menu (OrbitMenu.jsx, Follow-up 60), the same round button as the Showcase objects' categories, pinned bottom right. Each
// inspector keeps its own look in the ring, so no two look alike:
//   • arc            Sun: the pages are little suns on a warm dusk ring; the open one is the sun.
//   • strata         Atmosphere: sky strata over a planet's limb behind the ring.
//   • constellation  Stars: a night ring dusted with stars; the pages are four-pointed stars.
//   • aperture       Lens Flare: hexagonal ghosts strung along a flare streak.
//   • viewfinder     Camera: frame lines with corner marks; the open page carries the recording dot.
//   • phases         Moon: moon phases from crescent to full; the open page is the full moon.
//   • garage         Vehicles: a ring of tarmac with a lane line; the pages are rounded keys (VehicleAssetInspector.jsx).
// Every page stays in the document (hidden when shut) so its inputs keep their values and the BAKING page stays last. The open page is
// remembered per inspector. ← and → move between pages.

import React, { useState } from "react";
import { OrbitMenu } from "./OrbitMenu.jsx";
import "./InspectorTabs.css";

const Opened = {};
const Letter = (Index) => String.fromCharCode(65 + Index);
// Each look's page colours, in page order.
const Tones = {
  arc: ["#ffd36b", "#ffb238", "#ff8a3d", "#ff6b6b"],
  strata: ["#9fd4ff", "#6fb2ff", "#4a86e8", "#a78bfa"],
  constellation: ["#e6ecff", "#b7c6ff", "#8fa8ff", "#c4b5fd"],
  aperture: ["#7cf0ff", "#ffd36b", "#ff7ad9", "#a3ff8f"],
  viewfinder: ["#ff6b6b", "#ffd36b", "#9ad0ff", "#c2f970"],
  phases: ["#d7d9de", "#c3c7cf", "#aeb4bf", "#9aa2b0"],
  garage: ["#ffb238", "#7fd1ff", "#34c759", "#ff7a7a"],
};

export function InspectorTabs({ Owner, Look, Pages }) {
  const [Open, Choose] = useState(() => (Pages.some((Page) => Page.Id === Opened[Owner]) ? Opened[Owner] : Pages[0].Id));
  const Index = Math.max(0, Pages.findIndex((Page) => Page.Id === Open));
  const Pick = (At) => {
    Opened[Owner] = Pages[At].Id;
    Choose(Pages[At].Id);
  };
  const Palette = Tones[Look] || Tones.strata;
  const Choices = Pages.map((Page, At) => ({
    Id: Page.Id,
    Label: Page.Label,
    Tone: Palette[At % Palette.length],
    ClassName: "it-tab",
    Style: Look === "phases" ? { "--lit": 0.2 + (0.6 * At) / Math.max(1, Pages.length - 1) } : undefined,
    Inside: (
      <>
        <b>{Letter(At)}</b>
        <small>{Page.Label}</small>
      </>
    ),
    Extra: { id: `tab-${Owner}-${Page.Id}`, "aria-controls": `page-${Owner}-${Page.Id}`, "data-tab-for": `${Owner}:${Page.Id}` },
  }));
  return (
    <div className={"inspector-tabs it-" + Look} data-tabs={Owner} data-tabs-look={Look}>
      {Pages.map((Page, At) => (
        <section
          key={Page.Id}
          id={`page-${Owner}-${Page.Id}`}
          role="tabpanel"
          aria-label={`${Letter(At)} · ${Page.Label}`}
          className="it-page"
          data-tab-page={`${Owner}:${Page.Id}`}
          hidden={At !== Index}
        >
          {Page.Content}
        </section>
      ))}
      <OrbitMenu
        Owner={Owner}
        Look={Look}
        Pages={Choices}
        Index={Index}
        Pick={Pick}
        Face={Letter(Index)}
        Caption={`${Letter(Index)} · ${Pages[Index].Label}`}
        Middle={
          <>
            <b>{Letter(Index)}</b>
            <strong>{Pages[Index].Label}</strong>
            <span>
              {Index + 1} of {Pages.length}
            </span>
          </>
        }
      />
    </div>
  );
}
