import React, { useEffect, useState } from "react";
export default function AuthoringNameField({
  value,
  disabled = false,
  onChange,
  ariaLabel = "Layer name",
}) {
  const [draft, setDraft] = useState(value);
  useEffect(() => setDraft(value), [value]);
  return (
    <input
      aria-label={ariaLabel}
      value={draft}
      maxLength={80}
      disabled={disabled}
      onChange={(e) => setDraft(e.target.value)}
      onBlur={(e) => {
        const name = e.currentTarget.value.trim() || value;
        onChange(name);
        setDraft(name);
      }}
      onKeyDown={(e) => {
        if (e.key === "Enter") {
          e.preventDefault();
          e.currentTarget.blur();
        }
        if (e.key === "Escape") {
          e.stopPropagation();
          e.currentTarget.value = value;
          setDraft(value);
          e.currentTarget.blur();
        }
      }}
    />
  );
}
