import React, { useMemo, useState } from "react";
import { EntityNotes, Icon } from "./Inspectors.jsx";
import { FolderInventory, CollectionTypes } from "./FolderInventory.mjs";
import "./WorkspaceCards.css";
const Count = (N) => N.toLocaleString("en");
export default function FolderInspector({
  Subject,
  Rows,
  Hidden,
  Values,
  Change,
  Select,
  ToggleHidden,
}) {
  const Inventory = useMemo(
    () => FolderInventory(Rows, Subject.Id, Hidden),
    [Rows, Subject.Id, Hidden],
  );
  const [Query, Search] = useState(""),
    [Type, ChooseType] = useState("all"),
    [Scope, ChooseScope] = useState("all"),
    [Visibility, ChooseVisibility] = useState("all"),
    [Sort, ChooseSort] = useState("name"),
    [Page, ChoosePage] = useState(0),
    [Size, ChooseSize] = useState(25);
  const Items = useMemo(
    () =>
      Inventory.Entries.filter(
        ({ Row, Depth, Visible }) =>
          (Scope === "all" || Depth === 1) &&
          (Type === "all" || Row.Panel === Type) &&
          (Visibility === "all" || Visible === (Visibility === "visible")) &&
          `${Row.Name} ${Row.Id} ${Row.Description || ""}`
            .toLowerCase()
            .includes(Query.toLowerCase()),
      ).sort((A, B) =>
        Sort === "type"
          ? A.Row.Panel.localeCompare(B.Row.Panel) ||
            A.Row.Name.localeCompare(B.Row.Name)
          : Sort === "depth"
            ? A.Depth - B.Depth || A.Row.Name.localeCompare(B.Row.Name)
            : A.Row.Name.localeCompare(B.Row.Name, undefined, {
                numeric: true,
              }),
      ),
    [Inventory, Scope, Type, Visibility, Query, Sort],
  );
  const Pages = Math.max(1, Math.ceil(Items.length / Size)),
    Current = Math.min(Page, Pages - 1),
    Shown = Items.slice(Current * Size, (Current + 1) * Size),
    Total = Inventory.Entries.length;
  const Filter = (Set, Value) => {
    Set(Value);
    ChoosePage(0);
  };
  return (
    <div
      className="folder-inspector"
      data-panel="group"
      style={{ "--collection-tint": Values.Tint || "#9bacb4" }}
    >
      <header className="collection-heading">
        <div className="breadcrumbs">Inspector / Scene</div>
        <span className="workspace-eyebrow">COLLECTION / SCENE INVENTORY</span>
        <div>
          <Icon Name={Subject.Icon} Size={28} />
          <h1 title={Subject.Name}>{Subject.Name}</h1>
        </div>
        <p>
          {Subject.Description || "Scene collection"} · indexed from the current
          scene
        </p>
        <EntityNotes
          Value={Values.Notes || ""}
          Change={(Next) => Change("Notes", Next)}
        />
      </header>
      <nav
        className="collection-trail"
        aria-label="Collection path"
        title={Inventory.Trail.map((Row) => Row.Name).join(" / ")}
      >
        {Inventory.Trail.length > 4 && <span>… /</span>}
        {Inventory.Trail.slice(-4).map((Row) => (
          <button
            key={Row.Id}
            onClick={() => Select(Row.Id)}
            aria-current={Row.Id === Subject.Id ? "page" : undefined}
          >
            {Row.Name}
          </button>
        ))}
      </nav>
      <section className="workspace-card collection-total">
        <h2>
          <i />
          Collection contents
        </h2>
        <div className="workspace-number" data-collection-total={Total}>
          {Count(Total)}
          <small>entries</small>
        </div>
        <p>All descendants, including nested folders</p>
        <div className="collection-facts">
          <div>
            <b>{Count(Inventory.Direct)}</b>
            <span>Direct children</span>
          </div>
          <div>
            <b>{Count(Inventory.Folders)}</b>
            <span>Nested folders</span>
          </div>
          <div>
            <b>{Inventory.Depth}</b>
            <span>Levels below</span>
          </div>
        </div>
      </section>
      <div className="collection-status">
        <button
          className={
            "workspace-card " + (Visibility === "visible" ? "selected" : "")
          }
          onClick={() =>
            Filter(
              ChooseVisibility,
              Visibility === "visible" ? "all" : "visible",
            )
          }
          aria-pressed={Visibility === "visible"}
        >
          <i className="status-dot" />
          Visible<strong>{Count(Inventory.Visible)}</strong>
        </button>
        <button
          className={
            "workspace-card " + (Visibility === "hidden" ? "selected" : "")
          }
          onClick={() =>
            Filter(ChooseVisibility, Visibility === "hidden" ? "all" : "hidden")
          }
          aria-pressed={Visibility === "hidden"}
        >
          <i className="status-dot muted" />
          Hidden<strong>{Count(Total - Inventory.Visible)}</strong>
        </button>
      </div>
      <section className="workspace-card collection-composition">
        <h2>
          <i className="warm" />
          Composition <small>{Inventory.Types.length} types</small>
        </h2>
        <div
          className="collection-stack"
          aria-label="Collection type distribution"
        >
          {Inventory.Types.map(([Key, N], I) => (
            <button
              key={Key}
              title={`${CollectionTypes[Key] || Key}: ${Count(N)}`}
              style={{
                flex: N,
                background: [
                  "#b9c7ae",
                  "#9cabb9",
                  "#d1b898",
                  "#a6a198",
                  "#879c93",
                ][I % 5],
              }}
              onClick={() => Filter(ChooseType, Type === Key ? "all" : Key)}
              aria-label={`Filter ${CollectionTypes[Key] || Key}`}
            />
          ))}
        </div>
        <div className="collection-type-list">
          {Inventory.Types.map(([Key, N]) => (
            <button
              key={Key}
              aria-pressed={Type === Key}
              onClick={() => Filter(ChooseType, Type === Key ? "all" : Key)}
            >
              <span>{CollectionTypes[Key] || Key}</span>
              <b>{Count(N)}</b>
            </button>
          ))}
        </div>
        {!Total && (
          <p>This folder is empty. Newly added children will appear here.</p>
        )}
        <p>
          {Count(Inventory.Constructed)} constructed preview markers ·
          visibility includes ancestor folders. Counts are scene records, not
          render workload or memory usage.
        </p>
      </section>
      <section className="workspace-card collection-browser">
        <h2>
          <i />
          Browse contents <small>{Count(Items.length)} matches</small>
        </h2>
        <label className="collection-search">
          <span>Find an entry</span>
          <input
            type="search"
            aria-label="Search collection contents"
            placeholder="Name, ID or description…"
            value={Query}
            onChange={(E) => Filter(Search, E.target.value)}
          />
        </label>
        <div className="collection-filters">
          <label>
            Scope
            <select
              aria-label="Collection scope"
              value={Scope}
              onChange={(E) => Filter(ChooseScope, E.target.value)}
            >
              <option value="all">All descendants</option>
              <option value="direct">Direct children</option>
            </select>
          </label>
          <label>
            Type
            <select
              aria-label="Collection type"
              value={Type}
              onChange={(E) => Filter(ChooseType, E.target.value)}
            >
              <option value="all">All types</option>
              {Inventory.Types.map(([Key]) => (
                <option key={Key} value={Key}>
                  {CollectionTypes[Key] || Key}
                </option>
              ))}
            </select>
          </label>
          <label>
            Visibility
            <select
              aria-label="Collection visibility"
              value={Visibility}
              onChange={(E) => Filter(ChooseVisibility, E.target.value)}
            >
              <option value="all">All entries</option>
              <option value="visible">Visible</option>
              <option value="hidden">Hidden</option>
            </select>
          </label>
          <label>
            Sort
            <select
              aria-label="Collection sort"
              value={Sort}
              onChange={(E) => Filter(ChooseSort, E.target.value)}
            >
              <option value="name">Name A–Z</option>
              <option value="type">Type</option>
              <option value="depth">Hierarchy depth</option>
            </select>
          </label>
        </div>
        <div
          className="collection-results"
          role="list"
          aria-label="Collection results"
        >
          {Shown.map(({ Row, Depth, Visible, Inherited }) => (
            <div key={Row.Id} role="listitem" className="collection-entry">
              <button
                className="collection-entry-name"
                onClick={() => Select(Row.Id)}
                title={`${Row.Name}\n${Row.Id}\n${Row.Description || ""}`}
              >
                <Icon Name={Row.Icon} Size={21} />
                <span>
                  <b>{Row.Name}</b>
                  <small>
                    {CollectionTypes[Row.Panel] || Row.Panel} · level {Depth}
                  </small>
                  <small>
                    {Inherited && Row.Id !== "camera"
                      ? "Hidden by ancestor"
                      : Visible
                        ? "Visible"
                        : "Hidden"}{" "}
                    · {Inventory.ById.get(Row.Parent)?.Name || "Root"}
                  </small>
                </span>
              </button>
              <button
                className="collection-eye"
                disabled={Row.Id === "camera"}
                aria-label={`${Hidden[Row.Id] ? "Show" : "Hide"} collection entry ${Row.Name}`}
                title={
                  Row.Id === "camera"
                    ? "Editor Camera is permanently visible"
                    : Inherited
                      ? "Ancestor is hidden; this changes the entry’s own visibility flag"
                      : "Toggle own visibility"
                }
                onClick={() => ToggleHidden(Row.Id)}
              >
                <span className={"status-dot " + (!Visible ? "muted" : "")} />
              </button>
            </div>
          ))}
        </div>
        {!Items.length && (
          <div className="collection-empty">
            No matching entries.
            <button
              onClick={() => {
                Search("");
                ChooseType("all");
                ChooseScope("all");
                ChooseVisibility("all");
                ChoosePage(0);
              }}
            >
              Clear filters
            </button>
          </div>
        )}
        <footer className="collection-pagination">
          <span>
            {Items.length ? Count(Current * Size + 1) : 0}–
            {Count(Math.min((Current + 1) * Size, Items.length))} of{" "}
            {Count(Items.length)}
          </span>
          <div>
            <button
              aria-label="Previous collection page"
              disabled={!Current}
              onClick={() => ChoosePage(Current - 1)}
            >
              ←
            </button>
            <span>
              {Current + 1} / {Pages}
            </span>
            <button
              aria-label="Next collection page"
              disabled={Current + 1 >= Pages}
              onClick={() => ChoosePage(Current + 1)}
            >
              →
            </button>
            <select
              aria-label="Collection page size"
              value={Size}
              onChange={(E) => Filter(ChooseSize, +E.target.value)}
            >
              {[25, 50, 100].map((N) => (
                <option key={N}>{N}</option>
              ))}
            </select>
          </div>
        </footer>
      </section>
      <section className="workspace-card collection-notes">
        <h2>
          <i className="warm" />
          Collection notes
        </h2>
        <label className="collection-tint">
          Collection accent
          <input
            type="color"
            aria-label="Collection tint"
            value={Values.Tint || "#9bacb4"}
            onChange={(E) => Change("Tint", E.target.value)}
          />
        </label>
        <p>Identity colour only · child objects are unchanged.</p>
        <p>ID · {Subject.Id}</p>
      </section>
    </div>
  );
}
