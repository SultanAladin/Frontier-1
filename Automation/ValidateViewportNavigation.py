"""Source-level regression checks for world viewport mouse ownership (no GPU required)."""

from itertools import product
from pathlib import Path
import re
import unittest


Source = (Path(__file__).resolve().parents[1] / "Engine/Lifecycle/EditorLifecycle.cpp").read_text()
Interaction = Source.split("void EditorLifecycle::ProcessShowcaseViewportInteraction(", 1)[1]
Interaction = Interaction.split("// 4. Gizmo", 1)[0]


def EvaluatePredicate(Name, **Values):
    Match = re.search(r"const bool " + Name + r"\s*=\s*([^;]+);", Interaction)
    if Match is None:
        raise AssertionError("Missing navigation predicate: " + Name)
    Expression = Match.group(1)
    Expression = Expression.replace("WorldTransformGizmo.QueryDragging()", "GizmoBusy")
    Expression = Expression.replace("Io.WantTextInput", "TextInput")
    Expression = Expression.replace("&&", " and ").replace("||", " or ").replace("!", " not ")
    return bool(eval(Expression.strip(), {"__builtins__": {}}, Values))


class ViewportNavigationValidation(unittest.TestCase):
    def test_fly_requires_right_button_without_navigation_modifiers(self):
        for Right, Left, Middle, Alt, Busy, Text in product((False, True), repeat=6):
            Values = dict(RightHeld=Right, LeftHeld=Left, MiddleHeld=Middle,
                          AltHeld=Alt, GizmoBusy=Busy, TextInput=Text)
            Expected = Right and not Middle and not Alt and not Busy and not Text
            self.assertEqual(EvaluatePredicate("FlyKeysPossible", **Values), Expected, Values)
            self.assertEqual(EvaluatePredicate("WantCursorLock", **Values),
                             Right and not Middle and not Alt and not Busy, Values)

    def test_plain_left_button_does_not_latch_navigation(self):
        self.assertFalse(EvaluatePredicate("AnyNavButtonHeld", RightHeld=False,
                                          MiddleHeld=False, AltHeld=False, LeftHeld=True))
        self.assertTrue(EvaluatePredicate("AnyNavButtonHeld", RightHeld=False,
                                         MiddleHeld=False, AltHeld=True, LeftHeld=True))

    def test_left_camera_walk_removed(self):
        self.assertNotIn("GroundFwd", Interaction)
        self.assertNotIn("WalkOffset", Interaction)
        self.assertNotIn("(LeftHeld && RightHeld && !AltHeld)", Interaction)

    def test_navigation_cannot_begin_selection_or_gizmo_drag(self):
        self.assertIn("if (Input.PrimaryPointer.Pressed && !RightHeld && !MiddleHeld && !AltHeld)", Interaction)
        self.assertIn("if (!GizmoBusy)", Interaction)
        self.assertIn("WorldTransformGizmo.BeginDragInteraction", Interaction)
        self.assertIn("WorldTransformGizmo.EndDragInteraction()", Interaction)


if __name__ == "__main__":
    unittest.main()
