#!/usr/bin/env python3
"""AssertProofs — the applied-ink gate over every VisualProof shot.

Usage: python3 Tools/AssertProofs.py
Every assertion below was verified by hand against the reference sheets; the gate
fails loudly if a panel drifts from its applied ink.
"""

import sys

sys.path.insert(0, "Tools")
from ProofProbe import Decode, Sample

GATES = [
    # ① OutlinerHost — the standalone scene directory.
    ("VisualProof/OutlinerHost/directory.png", [
        (200, 250, 0x17171A, 8, "directory panel ground"),
        (37, 48, 0x000000, 6, "head black tile"),
        (250, 316, 0x232327, 8, "taken row ground (--row-sel)"),
        (87, 316, 0x4A90E2, 45, "taken rail (--accent)"),
        (200, 727, 0x101012, 6, "directory foot"),
    ]),
    # ② OutlinerHost — additive selection under control.
    ("VisualProof/OutlinerHost/multiselect.png", [
        (87, 316, 0x4A90E2, 45, "first taken rail"),
        (72, 412, 0x4A90E2, 45, "second taken rail"),
        (72, 444, 0x4A90E2, 45, "third taken rail"),
    ]),
    # ③ OutlinerHost — retention run `sk`.
    ("VisualProof/OutlinerHost/filter.png", [
        (150, 188, 0x232327, 8, "retained SK_BasePlate taken row"),
        (150, 250, 0x17171A, 8, "unretained Bodies row vacant"),
    ]),
    # ④ PanelValidationHost — texture texture, layers.
    ("VisualProof/PanelValidationHost/texturepaint-layers.png", [
        (200, 160, 0x121214, 8, "stack head"),
        (700, 205, 0x141414, 8, "add layer ground"),
        (1000, 160, 0x0E0E0E, 8, "channel head"),
        (1500, 350, 0x0E0E0E, 8, "chips region"),
    ]),
    # ⑤ PanelValidationHost — texture texture, mask.
    ("VisualProof/PanelValidationHost/texturepaint-mask.png", [
        (1000, 160, 0x0E0E0E, 8, "mask head"),
        (798, 400, 0x1C1C1C, 12, "section hair edge"),
        (1200, 840, 0x0E0E0E, 8, "mask foot"),
    ]),
    # ⑥ PanelValidationHost — texture texture, reorder drag.
    ("VisualProof/PanelValidationHost/texturepaint-reorder.png", [
        (400, 347, 0x4A90E2, 14, "insertion rail"),
    ]),
    # ⑦ PanelValidationHost — CAD parametric, properties page.
    ("VisualProof/PanelValidationHost/cad-properties.png", [
        (200, 160, 0x17171A, 8, "directory column ground"),
        (500, 160, 0x101012, 8, "properties bar column"),
        (1046, 217, 0x4A90E2, 16, "carousel properties underline"),
        (900, 263, 0x0A0A0B, 10, "record card ground"),
    ]),
    # ⑧ PanelValidationHost — CAD parametric, history page.
    ("VisualProof/PanelValidationHost/cad-history.png", [
        (1394, 217, 0x4A90E2, 16, "carousel history underline"),
        (889, 270, 0x4FD18B, 16, "cylinder revision bubble"),
    ]),

    # ─────────────────────────────────────────────────────────────────────
    # InterfaceValidationHost — the ported LayerstackV1 page. Every ink below
    # is a `LayerStackInk` token, so a drift in the token record fails here
    # rather than being noticed by eye three panels later.
    # ─────────────────────────────────────────────────────────────────────

    # ⑨ The page at rest.
    ("VisualProof/InterfaceValidationHost/layerstackpage.png", [
        (200, 20, 0x0A0A0A, 4, "stack head (--panel-2)"),
        (200, 110, 0x0D0D0D, 4, "quiet row (--row)"),
        (200, 480, 0x202020, 6, "taken row (--row-a)"),
        (600, 20, 0x0A0A0A, 4, "channel panel head"),
        (600, 300, 0x0D0D0D, 4, "channel blending row"),
        (600, 700, 0x0D0D0D, 4, "revisions row"),
    ]),
    # ⑩ A row hovered — the hover ink must differ from both quiet and taken.
    ("VisualProof/InterfaceValidationHost/layerstackhovered.png", [
        (200, 255, 0x161616, 4, "hovered mask row (--row-h)"),
        (200, 110, 0x0D0D0D, 4, "unroused row stays quiet"),
    ]),
    # ⑪ A press-and-release takes the row it landed on, and releases the previous one.
    ("VisualProof/InterfaceValidationHost/layerstacktaken.png", [
        (200, 300, 0x303030, 8, "newly taken row, hovered and taken together"),
        (200, 480, 0x0D0D0D, 4, "the previously taken row released"),
    ]),
    # ⑫ Taking a mask row swaps the inspector's second slide to the mask panel.
    ("VisualProof/InterfaceValidationHost/layerstackmasktaken.png", [
        (600, 100, 0x050505, 4, "mask panel body (--panel)"),
    ]),
    # ⑬ The add menu stands above every row.
    ("VisualProof/InterfaceValidationHost/layerstackaddmenu.png", [
        (430, 150, 0x050505, 5, "popup ground over the rows beneath it"),
    ]),
    # ⑭ The blend run, flipped above its anchor because it does not fit below.
    ("VisualProof/InterfaceValidationHost/layerstackblendmenu.png", [
        (110, 700, 0x0A0A0A, 4, "blend popup ground, applied above the footer pill"),
    ]),
    # ⑮ The wheel: a live hue ring, and the entry's own tag in the preview.
    ("VisualProof/InterfaceValidationHost/layerstackcolourwheel.png", [
        (270, 290, 0x9748E5, 20, "hue ring, violet quadrant"),
        (190, 465, 0xE5484D, 8, "preview carries the entry's applied colour tag"),
    ]),
    # ⑯ A carry in flight — the carried row dims and the drop rule stands.
    ("VisualProof/InterfaceValidationHost/layerstackcarried.png", [
        (200, 160, 0x0E0E0E, 4, "carried row scrimmed in place"),
        (200, 412, 0xFFFFFF, 6, "drop rule at the destination's lower edge"),
    ]),
    # ⑰ The carry released — the arrangement moved, and the ring recorded it.
    ("VisualProof/InterfaceValidationHost/layerstackdropped.png", [
        (200, 430, 0x202020, 6, "the carried row, taken at its new seat"),
        # 📐 The pane grew a 34px action bar above its run, so the first medallion moved with it.
        (417, 714, 0xFFFFFF, 6, "this session's revision medallion in the History pane"),
    ]),
    # ⑱ The stack scrolled by the wheel, with its bar advanced to match.
    ("VisualProof/InterfaceValidationHost/layerstackscrolled.png", [
        (386, 300, 0x2A2A2A, 8, "scroll thumb, advanced off the top of its travel"),
    ]),
    # ⑳ The revision pane at rest: the ordinal medallion, the node on the spine, and the spine itself
    #     standing unbroken through the gap between two cards.
    ("VisualProof/InterfaceValidationHost/revisions.png", [
        ( 16, 100, 0xFFFFFF, 6, "ordinal-zero medallion, carried in the accent"),
        ( 39,  99, 0xFFFFFF, 6, "the node applied on the spine, 19px into the first card"),
        ( 39, 130, 0x1E1E1E, 6, "the spine, unbroken across the gap between two cards"),
        (200, 110, 0x0D0D0D, 5, "a card at rest, on the row ground"),
    ]),
    # ㉑ The third card unfolded by a press: its fold stands, carrying a Comment field.
    ("VisualProof/InterfaceValidationHost/revisionsunfolded.png", [
        (200, 228, 0x101010, 5, "the unfolded card's fold ground"),
        (200, 196, 0x121212, 5, "the unfolded card's own head, lifted off the row ground"),
        (200, 340, 0x0D0D0D, 5, "the card beneath the fold, pushed down by it"),
    ]),
    # ㉓ The content browser at rest — every column of the port stands, and nothing is taken.
    ("VisualProof/InterfaceValidationHost/contentbrowser.png", [
        ( 120, 500, 0x0A0A0A, 3, "the sources aside, bg-[#0a0a0a]"),
        ( 700, 600, 0x000000, 3, "the lattice ground behind the records, bg-black"),
        (1400, 600, 0x0A0A0A, 3, "the inspector column, bg-[#0a0a0a]"),
        ( 480,  25, 0x111111, 4, "the seek field, bg-[#111]"),
        (1200,  25, 0xFFFFFF, 4, "the import action, filled white as the reference has it"),
    ]),
    # ㉔ A record taken — the inspector fills, and the card carries border-white/60 down both its sides.
    ("VisualProof/InterfaceValidationHost/contentbrowsertaken.png", [
        ( 869, 200, 0xA3A3A5, 8, "the taken card's leading edge, white/60 over its own plate"),
        (1058, 200, 0xA3A3A5, 8, "the taken card's trailing edge, the same white/60"),
        ( 669, 200, 0x1A1A1E, 4, "an untaken card at the same depth, bare plate and no white edge"),
        (1300, 870, 0xFFFFFF, 4, "the inspector's import action, which only stands once a record is taken"),
    ]),
    # ㉕ A seek run entered — the lattice retains only what answers it, and the rest of the run is bare.
    ("VisualProof/InterfaceValidationHost/contentbrowserseek.png", [
        (1000, 200, 0x000000, 3, "bare lattice ground where a record stood before the seek retained"),
        ( 344, 200, 0x1A1A1E, 4, "the first retained record still standing on its plate"),
    ]),
    # ㉖ An archive traversed from the aside — the traversed row is taken, and the breadcrumb follows it.
    ("VisualProof/InterfaceValidationHost/contentbrowsertraversed.png", [
        ( 130, 215, 0x222222, 5, "the traversed source row, bg-white/10 over the aside"),
        ( 130, 120, 0x0A0A0A, 3, "an untraversed row beside it, bare aside ground"),
    ]),
    # ㉒ The whole run folded shut at the head, which must record nothing beneath the head at all.
    ("VisualProof/InterfaceValidationHost/revisionsfolded.png", [
        (200, 201, 0x050505, 3, "bare panel ground where the folded run records nothing"),
        (200,  16, 0x161616, 6, "the head, hovered under the pointer that folded it"),
    ]),
]


def Main():
    Failures = 0
    Total = 0
    for Path, Probes in GATES:
        Width, Height, Pixels = Decode(Path)
        for X, Y, Expected, Tolerance, Label in Probes:
            Total += 1
            Red, Green, Blue, _ = Sample(Pixels, Width, X, Y)
            Passed = (abs(Red - ((Expected >> 16) & 0xFF)) <= Tolerance and
                      abs(Green - ((Expected >> 8) & 0xFF)) <= Tolerance and
                      abs(Blue - (Expected & 0xFF)) <= Tolerance)
            if not Passed:
                Failures += 1
                Got = (Red << 16) | (Green << 8) | Blue
                print(f"  [FAIL] {Path} ({X},{Y}) {Label}: want #{Expected:06x} got #{Got:06x}")
    print(f"AssertProofs: {Total - Failures}/{Total} applied inks stand")
    raise SystemExit(1 if Failures else 0)


if __name__ == "__main__":
    Main()
