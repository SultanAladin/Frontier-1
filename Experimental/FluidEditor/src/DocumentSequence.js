const Select = (Selector) => document.querySelector(Selector);
const Escape = (Text) =>
  String(Text).replace(
    /[&<>"']/g,
    (Character) =>
      ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[
        Character
      ],
  );
const Fields = [
  "Parameters",
  "Camera",
  "Engine",
  "Canvas",
  "Names",
  "Selection",
  "Tab",
  "Preset",
  "Dirty",
  "Bursts",
  "SavedCollider",
  "SavedSun",
  "Backend",
];

export class DocumentSequence {
  constructor(Editor, Icon) {
    this.Editor = Editor;
    this.Icon = Icon;
    this.NextIdentity = 2;
    this.ActiveIdentity = 1;
    this.Slots = [
      { Identity: 1, Name: Select("#document-name").value, State: null },
    ];
    this.ConstructTabs();
    Select("#new-document").addEventListener("click", () =>
      this.OpenDocument(),
    );
    Select("#document-tabs").addEventListener("click", (Event) => {
      const Close = Event.target.closest("[data-close-document]");
      if (Close) {
        this.CloseDocument(Number(Close.dataset.closeDocument));
        return;
      }
      if (Event.target.matches("input")) return;
      const Tab = Event.target.closest("[data-document]");
      if (Tab) this.ActivateDocument(Number(Tab.dataset.document));
    });
    Select("#document-tabs").addEventListener("dblclick", (Event) => {
      if (Event.target.closest("[data-close-document]")) return;
      const Tab = Event.target.closest("[data-document]");
      if (!Tab || Number(Tab.dataset.document) !== this.ActiveIdentity) return;
      const Input = Tab.querySelector("input");
      Input.hidden = false;
      Tab.querySelector(".document-label").hidden = true;
      Input.value = this.Slots.find(
        (Slot) => Slot.Identity === this.ActiveIdentity,
      ).Name;
      Input.focus();
      Input.select();
    });
    Select("#document-tabs").addEventListener("focusout", (Event) => {
      if (Event.target.matches(".document-rename") && !Event.target.hidden)
        this.RenameDocument(Event.target);
    });
    Select("#document-tabs").addEventListener("keydown", (Event) => {
      if (Event.target.matches("input")) {
        if (Event.key === "Enter") {
          Event.preventDefault();
          this.RenameDocument(Event.target);
        }
        if (Event.key === "Escape") {
          Event.preventDefault();
          Event.target.hidden = true;
          Event.target.parentElement.querySelector(".document-label").hidden =
            false;
          Event.target.parentElement.focus();
        }
        Event.stopPropagation();
        return;
      }
      const Tab = Event.target.closest("[data-document]");
      if (Tab && (Event.key === "Enter" || Event.key === " ")) {
        Event.preventDefault();
        Event.stopPropagation();
        this.ActivateDocument(Number(Tab.dataset.document));
      }
      if (Tab && ["ArrowLeft", "ArrowRight"].includes(Event.key)) {
        Event.preventDefault();
        Event.stopPropagation();
        const Index = this.Slots.findIndex(
          (Slot) => Slot.Identity === Number(Tab.dataset.document),
        );
        const Next =
          this.Slots[
            (Index + (Event.key === "ArrowRight" ? 1 : this.Slots.length - 1)) %
              this.Slots.length
          ];
        this.ActivateDocument(Next.Identity);
        Select(`[data-document="${Next.Identity}"]`).focus();
      }
    });
  }

  CaptureDocument() {
    const Slot = this.Slots.find(
      (Document) => Document.Identity === this.ActiveIdentity,
    );
    Slot.Name = Select("#document-name").value.trim() || "Untitled";
    this.Editor.Engine?.setBrush(null, null, false);
    Slot.State = Object.fromEntries(
      Fields.map((Field) => [Field, this.Editor[Field]]),
    );
    Slot.State.CameraView = Select("#camera-view").value;
    Slot.State.BurstKind = Select("#burst-kind").value;
    Slot.State.Fault = Select("#gpu-error").hidden
      ? ""
      : Select("#gpu-error-message").textContent;
    return Slot;
  }

  RefreshLabels() {
    const Active = this.Slots.find(
      (Slot) => Slot.Identity === this.ActiveIdentity,
    );
    if (!Active) return;
    Active.Name = Select("#document-name").value.trim() || "Untitled";
    for (const Slot of this.Slots) {
      const Tab = Select(`[data-document="${Slot.Identity}"]`);
      if (!Tab) continue;
      const Selected = Slot.Identity === this.ActiveIdentity;
      Tab.setAttribute("aria-selected", String(Selected));
      Tab.classList.toggle("active", Selected);
      Tab.querySelector(".document-label").textContent = `${Slot.Name}.fluid`;
      Tab.querySelector(".tab-dirty").hidden = !(Selected
        ? this.Editor.Dirty
        : Slot.State?.Dirty);
      Tab.title = `${Slot.Name}.fluid · Double-click to rename${Selected ? "" : " · Simulation held while inactive"}`;
    }
  }

  ConstructTabs() {
    Select("#document-tabs").innerHTML = this.Slots.map(
      (Slot) =>
        `<div class="document-tab" role="tab" aria-controls="fluid-workspace" aria-selected="${Slot.Identity === this.ActiveIdentity}" tabindex="0" data-document="${Slot.Identity}">${this.Icon("flame")}<span class="document-label">${Escape(Slot.Name)}.fluid</span><input class="document-rename" aria-label="Rename document" maxlength="64" hidden/><span class="tab-dirty" hidden></span><button class="tab-close" data-close-document="${Slot.Identity}" aria-label="Close ${Escape(Slot.Name)}" title="Close document" ${this.Slots.length === 1 ? "disabled" : ""}>${this.Icon("close")}</button></div>`,
    ).join("");
    this.RefreshLabels();
  }

  RenameDocument(Input) {
    const Name = Input.value.trim() || "Untitled";
    Input.hidden = true;
    Input.parentElement.querySelector(".document-label").hidden = false;
    Select("#document-name").value = Name;
    this.Editor.MarkDirty();
    this.RefreshLabels();
    Input.parentElement.focus();
  }

  async OpenDocument() {
    if (this.Editor.Switching || this.Editor.Baking) return;
    if (this.Slots.length >= 4) {
      this.Editor.Notify(
        "Four live documents maximum to bound GPU memory. Save and close a tab before adding another.",
      );
      return;
    }
    this.Slots.push({
      Identity: this.NextIdentity++,
      Name: `Untitled ${this.NextIdentity - 2}`,
      State: null,
    });
    this.ConstructTabs();
    await this.ActivateDocument(this.Slots.at(-1).Identity);
  }

  async ActivateDocument(Identity) {
    if (
      Identity === this.ActiveIdentity ||
      this.Editor.Switching ||
      this.Editor.Baking
    )
      return;
    const Slot = this.Slots.find((Document) => Document.Identity === Identity);
    if (!Slot) return;
    this.CaptureDocument();
    const PreviousCanvas = this.Editor.Canvas;
    this.ActiveIdentity = Identity;
    Select("#document-name").value = Slot.Name;
    if (Slot.State) {
      for (const Field of Fields) this.Editor[Field] = Slot.State[Field];
      PreviousCanvas.replaceWith(this.Editor.Canvas);
      Select("#camera-view").value = Slot.State.CameraView;
      Select("#burst-kind").value = Slot.State.BurstKind;
      Select("#gpu-error").hidden = !Slot.State.Fault;
      Select("#gpu-error-message").textContent = Slot.State.Fault;
    } else {
      this.Editor.InitializeDocumentState();
      this.Editor.Canvas = PreviousCanvas.cloneNode(false);
      PreviousCanvas.replaceWith(this.Editor.Canvas);
      this.Editor.Engine = null;
      await this.Editor.InitializeRenderer("webgl2");
      Select("#camera-view").value = "perspective";
      Select("#burst-kind").value = "burst";
    }
    Select("#document-name").value = Slot.Name;
    Select("#backend-select").value = this.Editor.Backend || "webgl2";
    Select("#status-ready").textContent = this.Editor.Engine
      ? "GPU ready"
      : "GPU unavailable";
    Select("#viewport-subtitle").textContent =
      this.Editor.Backend === "webgpu"
        ? "Experimental gas path · WebGL2 provides full effects"
        : "Eulerian volume · physically shaded";
    this.Editor.SingleStep = false;
    this.Editor.LastTime = performance.now();
    this.Editor.Elapsed = 0;
    this.Editor.Frames = 0;
    this.Editor.SceneFilter = "all";
    Select("#scene-search").value = "";
    Select("#collection-toggle").setAttribute("aria-expanded", "true");
    this.Editor.ResizeViewport();
    this.Editor.ConstructSceneRows();
    this.Editor.ConstructInspector();
    this.Editor.ConstructPresetCards();
    this.Editor.RefreshControls();
    this.RefreshLabels();
    if (this.Editor.Engine?.gl?.isContextLost())
      this.Editor.ShowGpuError(
        "This document lost its GPU context. Retry to rebuild it from the saved settings.",
      );
  }

  async CloseDocument(Identity) {
    if (this.Slots.length === 1 || this.Editor.Switching || this.Editor.Baking)
      return;
    const Slot = this.Slots.find((Document) => Document.Identity === Identity);
    if (!Slot) return;
    const Dirty =
      Identity === this.ActiveIdentity ? this.Editor.Dirty : Slot.State?.Dirty;
    if (
      Dirty &&
      !window.confirm(`Close “${Slot.Name}” and discard its unsaved settings?`)
    )
      return;
    if (Identity === this.ActiveIdentity)
      await this.ActivateDocument(
        this.Slots.find((Document) => Document !== Slot).Identity,
      );
    Slot.State?.Engine?.destroy();
    Slot.State?.Engine?.gl?.getExtension("WEBGL_lose_context")?.loseContext();
    this.Slots = this.Slots.filter((Document) => Document !== Slot);
    this.ConstructTabs();
  }
}
