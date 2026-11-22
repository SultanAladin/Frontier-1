// Owned, build-time presentation adaptation; pinned inspector sources remain byte-identical.
// Every replacement must match once so an upstream change cannot silently drop the requested style.
export function RecolourInstrument(Name, Source) {
  const Replace = (Before, After) => {
    if (Source.split(Before).length !== 2)
      throw new Error(
        `Instrument presentation anchor changed: ${Name}: ${Before.slice(0, 65)}`,
      );
    Source = Source.replace(Before, () => After);
  };
  if (Name === "wind.js") {
    Replace(
      `    /* the mean, and the band the gusts live in */
    const mean = SPD();
    const hi = Math.max(...TRACE), lo = Math.min(...TRACE);
    g.fillStyle = 'rgba(137,224,196,.07)';
    g.fillRect(px(0), py(hi), px(1) - px(0), Math.max(1, py(lo) - py(hi)));`,
      `    /* Neutral under-trace wash; no green rectangular gust band. */
    const mean = SPD();
    const Wash = g.createLinearGradient(0, T, 0, h - B);
    Wash.addColorStop(0, 'rgba(224,224,224,.10)');
    Wash.addColorStop(1, 'rgba(224,224,224,.01)');
    g.beginPath(); g.moveTo(px(0), py(0));
    TRACE.forEach((Speed, Index) => g.lineTo(px(Index / (TRACE.length - 1)), py(Speed)));
    g.lineTo(px(1), py(0)); g.closePath(); g.fillStyle = Wash; g.fill();`,
    );
    Replace(
      "g.strokeStyle = 'rgba(137,224,196,.9)';",
      "g.strokeStyle = 'rgba(210,210,210,.36)';",
    );
    Replace(
      `    g.lineWidth = 1.4;
    g.stroke();
    g.lineWidth = 1;
    g.fillStyle = '#fff';`,
      `    g.lineWidth = 1.2;
    g.stroke();
    // Highlight the actual above-mean samples, not a separate invented trace.
    g.beginPath();
    for (let Index = 1; Index < TRACE.length; Index++) {
      if (TRACE[Index] <= mean || TRACE[Index - 1] <= mean) continue;
      g.moveTo(px((Index - 1) / (TRACE.length - 1)), py(TRACE[Index - 1]));
      g.lineTo(px(Index / (TRACE.length - 1)), py(TRACE[Index]));
    }
    g.strokeStyle = 'rgba(242,242,242,.95)'; g.stroke(); g.lineWidth = 1;
    const Peak = TRACE.indexOf(Math.max(...TRACE)), Trough = TRACE.indexOf(Math.min(...TRACE));
    [[Peak, '#eeeeee'], [Trough, '#d69a54']].forEach(([Index, Colour]) => {
      g.fillStyle = Colour; g.beginPath();
      g.arc(px(Index / (TRACE.length - 1)), py(TRACE[Index]), 2.3, 0, Math.PI * 2); g.fill();
    });
    g.fillStyle = '#fff';`,
    );
  } else if (Name === "fog.js") {
    Replace(
      "const [g, w, h] = sizeCanvas(chamberCv, 74); g.clearRect(0, 0, w, h); g.fillStyle = '#191919'; g.fillRect(0, 0, w, h);",
      "const [g, w, h] = sizeCanvas(chamberCv, 74); g.clearRect(0, 0, w, h);",
    );
  }
  return Source;
}
