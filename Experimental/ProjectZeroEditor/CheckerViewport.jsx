import React from "react";

export function PreviewVisible(Subject, Rows, Hidden) {
  const Seen = new Set();
  let Current = Subject;
  while (Current) {
    if (Hidden[Current.Id] || Seen.has(Current.Id)) return false;
    Seen.add(Current.Id);
    Current = Rows.find((Entry) => Entry.Id === Current.Parent);
  }
  return true;
}

function Shape({ Subject }) {
  const Artwork = Subject.Icon;
  if (Artwork === "editor-cube")
    return (
      <>
        <path d="m40 7 27 15v31L40 69 13 53V22Z" />
        <path d="m13 22 27 16 27-16M40 38v31M40 7v31" />
      </>
    );
  if (Artwork === "editor-cone")
    return (
      <>
        <path d="M14 57 40 9l26 48" />
        <ellipse cx="40" cy="57" rx="26" ry="10" />
      </>
    );
  if (Artwork === "editor-cylinder")
    return (
      <>
        <ellipse cx="40" cy="18" rx="25" ry="10" />
        <path d="M15 18v39c0 14 50 14 50 0V18" />
        <path d="M15 57c0-14 50-14 50 0" strokeDasharray="3 4" />
      </>
    );
  if (Artwork === "editor-torus")
    return (
      <>
        <ellipse cx="40" cy="38" rx="30" ry="23" />
        <ellipse cx="40" cy="38" rx="15" ry="10" />
      </>
    );
  if (Artwork === "editor-sphere" || Subject.Panel === "moon")
    return (
      <>
        <circle cx="40" cy="38" r="28" />
        <ellipse cx="40" cy="38" rx="12" ry="28" />
        <ellipse cx="40" cy="38" rx="28" ry="10" />
      </>
    );
  if (Subject.Panel === "camera")
    return (
      <>
        <rect x="11" y="21" width="38" height="31" rx="4" />
        <path d="m49 29 20-10v35L49 44M22 52l-7 16M36 52l8 16" />
      </>
    );
  if (Subject.Panel === "light" || Subject.Panel === "sun")
    return (
      <>
        <circle cx="40" cy="38" r="17" />
        {Array.from({ length: 8 }, (_, Index) => (
          <path
            key={Index}
            d="M40 9v7"
            transform={`rotate(${Index * 45} 40 38)`}
          />
        ))}
      </>
    );
  if (["local-cloud", "local-fog"].includes(Subject.Panel))
    return (
      <rect x="12" y="12" width="56" height="52" rx="3" strokeDasharray="5 5" />
    );
  if (Subject.Panel === "rainbow")
    return (
      <>
        <path d="M10 61a30 36 0 0 1 60 0M18 61a22 27 0 0 1 44 0M26 61a14 18 0 0 1 28 0" />
      </>
    );
  if (Subject.Panel === "wind")
    return (
      <>
        <path d="M9 28h45c15 0 15-22 0-22M9 39h52c17 0 17 23 0 23M9 50h27" />
        <path d="m34 19 10 9-10 9" />
      </>
    );
  if (Subject.Panel === "stars")
    return <path d="m40 6 8 23 25 9-25 9-8 23-8-23-25-9 25-9Z" />;
  if (Subject.Panel === "clouds" || Subject.Panel === "precipitation")
    return (
      <>
        <path d="M23 46h36a12 12 0 0 0 0-24 19 19 0 0 0-35-4 14 14 0 0 0-1 28Z" />
        {Subject.Panel === "precipitation" && (
          <path d="m26 53-4 12m20-12-4 12m20-12-4 12" />
        )}
      </>
    );
  if (Subject.Panel.includes("fog"))
    return <path d="M12 23h46M23 35h45M10 47h44M28 59h37" />;
  return (
    <>
      <circle cx="40" cy="38" r="26" />
      <path d="M30 38h20M40 28v20" />
    </>
  );
}

export default function CheckerViewport({
  Rows,
  Hidden,
  Selected,
  Select,
  View = 1,
}) {
  const Added = Rows.filter((Subject) => Subject.Preview);
  const Visible = Added.filter((Subject) =>
    PreviewVisible(Subject, Rows, Hidden),
  );
  return (
    <div
      className="checker-pane"
      role="region"
      aria-label={
        View === 1 ? "Checkerboard viewport" : "Second checkerboard viewport"
      }
    >
      <div className="checker-content">
        {Visible.length ? (
          <div className="checker-placements">
            {Visible.map((Subject) => (
              <button
                key={Subject.Id}
                data-preview-id={Subject.Id}
                className={
                  "preview-placement " +
                  (Subject.Id === Selected ? "selected" : "")
                }
                aria-label={`Select ${Subject.Name} in viewport`}
                aria-pressed={Subject.Id === Selected}
                onClick={() => Select(Subject.Id)}
                title={`${Subject.Name} · analytical placeholder, not rendered geometry`}
              >
                <svg viewBox="0 0 80 76" aria-hidden="true">
                  <Shape Subject={Subject} />
                </svg>
                <span>{Subject.Name}</span>
              </button>
            ))}
          </div>
        ) : (
          <div className="checker-empty">
            <span>
              {Added.length
                ? "Constructed entities are hidden"
                : "No constructed entities"}
            </span>
            <small>
              {Added.length
                ? "Use the outliner to show them"
                : "Ctrl+A to construct"}
            </small>
          </div>
        )}
      </div>
      <span className="checker-caption">HTML PREVIEW · ANALYTICAL MARKERS</span>
    </div>
  );
}
