/*============================================================================================================================================
                                                             STUDIOTOOLTIPS.JSX                                                             
============================================================================================================================================*/
// 🧩 Theme-inheriting tooltip placement and shared pointer/keyboard disclosure.

import React, { useEffect, useLayoutEffect, useRef, useState } from "react";
import { createPortal } from "react-dom";
import "./studioTooltips.css";

// 🧩 One delegated tooltip surface for all HTML workspaces, including portalled controls.
// Optional metadata: data-tooltip-title/body/shortcut/placement. No tool owns its colours.
export default function StudioTooltips()
{
  const [Tip, SetTip] = useState(null);
  const [Position, SetPosition] = useState(null);
  const Card = useRef(null);
  const Active = useRef(null);
  const Close = useRef(() => {});
  useEffect(() =>
  {
    let Timer;
    let Dismissed;
    let Showing = false;
    function Hide()
    {
      clearTimeout(Timer);
      Showing = false;
      const Previous = Active.current;
      if (Previous)
      {
        if (Previous.NativeTitle && !Previous.Element.hasAttribute("title")) Previous.Element.setAttribute("title", Previous.NativeTitle);
        if (Previous.GeneratedLabel && Previous.Element.getAttribute("aria-label") === Previous.GeneratedLabel) Previous.Element.removeAttribute("aria-label");
        const Descriptions = (Previous.Element.getAttribute("aria-describedby") || "").split(/\s+/).filter(Value => Value && Value !== "studio-tool-description");
        if (Descriptions.length) Previous.Element.setAttribute("aria-describedby", Descriptions.join(" "));
        else Previous.Element.removeAttribute("aria-describedby");
      }
      Active.current = null;
      SetTip(null);
      SetPosition(null);
    }
    Close.current = Hide;
    function FindTrigger(Target)
    {
      return Target instanceof Element ? Target.closest("[data-tooltip-title], [title], button, [role=button][aria-label]") : null;
    }
    function Schedule(Element, Immediate)
    {
      if (!Element || Element === Dismissed || Element.closest("[role=tooltip]")) return;
      if (Active.current?.Element === Element) return;
      Hide();
      const NativeTitle = Element.getAttribute("title");
      const Caption = Element.textContent?.trim();
      const Heading = Element.dataset.tooltipTitle || Element.getAttribute("aria-label") || (Caption?.length > 2 && Caption.length <= 64 ? Caption : NativeTitle) || NativeTitle || Caption;
      if (!Heading?.trim()) return;
      const GeneratedLabel = NativeTitle && !Element.hasAttribute("aria-label") && !Element.textContent?.trim() ? NativeTitle : null;
      if (GeneratedLabel) Element.setAttribute("aria-label", GeneratedLabel);
      Active.current = { Element, NativeTitle, GeneratedLabel };
      // Suppress the browser bubble while preserving title-based integration outside hover/focus.
      if (NativeTitle) Element.removeAttribute("title");
      Timer = setTimeout(() =>
      {
        if (!Element.isConnected || Active.current?.Element !== Element) return Hide();
        Showing = true;
        const Existing = Element.getAttribute("aria-describedby") || "";
        Element.setAttribute("aria-describedby", `${Existing} studio-tool-description`.trim());
        const Theme = getComputedStyle(Element);
        SetTip({ Element, Heading, Body: Element.dataset.tooltipBody || (NativeTitle !== Heading ? NativeTitle : ""), Shortcut: Element.dataset.tooltipShortcut, Placement: Element.dataset.tooltipPlacement,
          Theme: { background: Theme.getPropertyValue("--tooltip-surface") || Theme.getPropertyValue("--panel") || "#171717", color: Theme.getPropertyValue("--tooltip-text") || Theme.color, borderColor: Theme.getPropertyValue("--border") || "#444", fontFamily: Theme.fontFamily, "--tooltip-muted": Theme.getPropertyValue("--muted") || "#999", "--tooltip-accent": Theme.getPropertyValue("--accent") || Theme.color } });
      }, Immediate ? 0 : 420);
    }
    function PointerOver(Event)
    {
      if (Event.pointerType === "touch") return;
      const Element = FindTrigger(Event.target);
      if (Element !== FindTrigger(Event.relatedTarget)) { Dismissed = null; Schedule(Element, false); }
    }
    function PointerOut(Event)
    {
      if (!Active.current?.Element.contains(Event.relatedTarget)) Hide();
      if (FindTrigger(Event.target) !== FindTrigger(Event.relatedTarget)) Dismissed = null;
    }
    function FocusIn(Event)
    {
      const Element = FindTrigger(Event.target);
      if (Element?.matches(":focus-visible")) { Dismissed = null; Schedule(Element, true); }
    }
    function Dismiss()
    {
      Dismissed = Active.current?.Element;
      Hide();
    }
    function KeyDown(Event)
    {
      if (Event.key !== "Escape") return;
      const PopupOwnsEscape = Event.target instanceof Element && Event.target.closest("[role=menu], [role=listbox], [data-tooltip-escape-passthrough]");
      if (Showing && !PopupOwnsEscape)
      {
        Event.preventDefault();
        Event.stopPropagation();
      }
      Dismiss();
    }
    document.addEventListener("pointerover", PointerOver);
    document.addEventListener("pointerout", PointerOut);
    document.addEventListener("focusin", FocusIn);
    document.addEventListener("focusout", Hide);
    document.addEventListener("pointerdown", Dismiss, true);
    document.addEventListener("keydown", KeyDown, true);
    document.addEventListener("scroll", Dismiss, true);
    window.addEventListener("resize", Dismiss);
    window.addEventListener("blur", Dismiss);
    return () =>
    {
      Hide();
      document.removeEventListener("pointerover", PointerOver);
      document.removeEventListener("pointerout", PointerOut);
      document.removeEventListener("focusin", FocusIn);
      document.removeEventListener("focusout", Hide);
      document.removeEventListener("pointerdown", Dismiss, true);
      document.removeEventListener("keydown", KeyDown, true);
      document.removeEventListener("scroll", Dismiss, true);
      window.removeEventListener("resize", Dismiss);
      window.removeEventListener("blur", Dismiss);
    };
  }, []);
  useLayoutEffect(() =>
  {
    if (!Tip || !Card.current) return;
    const Anchor = Tip.Element.getBoundingClientRect();
    if (!Tip.Element.isConnected || Anchor.width === 0 || Anchor.height === 0) return Close.current();
    const Bounds = Card.current.getBoundingClientRect();
    const Margin = 12, Gap = 13;
    const Space = { bottom: window.innerHeight - Anchor.bottom, top: Anchor.top, right: window.innerWidth - Anchor.right, left: Anchor.left };
    const Required = Side => ["left", "right"].includes(Side) ? Bounds.width + Gap + Margin : Bounds.height + Gap + Margin;
    const Preference = [Tip.Placement, "bottom", "top", "right", "left"].filter(Side => Side in Space);
    const Side = Preference.find(Value => Space[Value] >= Required(Value)) || Object.keys(Space).sort((Left, Right) => Space[Right] - Space[Left])[0];
    let Left = Anchor.left + Anchor.width / 2 - Bounds.width / 2;
    let Top = Anchor.top + Anchor.height / 2 - Bounds.height / 2;
    if (Side === "bottom") Top = Anchor.bottom + Gap;
    if (Side === "top") Top = Anchor.top - Bounds.height - Gap;
    if (Side === "right") Left = Anchor.right + Gap;
    if (Side === "left") Left = Anchor.left - Bounds.width - Gap;
    Left = Math.max(Margin, Math.min(window.innerWidth - Bounds.width - Margin, Left));
    Top = Math.max(Margin, Math.min(window.innerHeight - Bounds.height - Margin, Top));
    const Horizontal = ["top", "bottom"].includes(Side);
    const Pointer = Horizontal ? Anchor.left + Anchor.width / 2 - Left : Anchor.top + Anchor.height / 2 - Top;
    SetPosition({ left: Left, top: Top, Side, "--tooltip-pointer": `${Math.max(20, Math.min((Horizontal ? Bounds.width : Bounds.height) - 20, Pointer))}px` });
  }, [Tip]);
  if (!Tip) return null;
  const { Side, ...Coordinates } = Position || {};
  return createPortal(<div id="studio-tool-description" ref={Card} role="tooltip" className="studio-tooltip-card" data-placement={Side} style={{ ...Tip.Theme, ...Coordinates, visibility: Position ? "visible" : "hidden" }}>
    <div className="studio-tooltip-heading">{Tip.Heading}{Tip.Shortcut && <kbd>{Tip.Shortcut}</kbd>}</div>
    {Tip.Body && <p>{Tip.Body}</p>}
  </div>, document.body);
}
