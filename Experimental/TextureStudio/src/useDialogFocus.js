import { useEffect } from "react";
// Small shared focus guard; no prototype runtime or second UI library required.
export default function useDialogFocus(open, ref) {
  useEffect(() => {
    if (!open) return;
    const previous = document.activeElement,
      dialog = ref.current;
    const focusables = () =>
      [
        ...dialog.querySelectorAll(
          'button:not(:disabled), input:not(:disabled), select:not(:disabled), [tabindex="0"]',
        ),
      ].filter((e) => e.getClientRects().length);
    (
      dialog.querySelector("[data-dialog-autofocus]") ||
      focusables()[0] ||
      dialog
    )?.focus();
    function trap(event) {
      if (event.key !== "Tab") return;
      const list = focusables(),
        first = list[0],
        last = list.at(-1);
      if (!list.length) {
        event.preventDefault();
        dialog.focus();
      } else if (
        !dialog.contains(document.activeElement) ||
        (event.shiftKey && document.activeElement === first)
      ) {
        event.preventDefault();
        (event.shiftKey ? last : first).focus();
      } else if (!event.shiftKey && document.activeElement === last) {
        event.preventDefault();
        first.focus();
      }
    }
    window.addEventListener("keydown", trap, true);
    return () => {
      window.removeEventListener("keydown", trap, true);
      if (previous?.isConnected) previous.focus();
    };
  }, [open, ref]);
}
