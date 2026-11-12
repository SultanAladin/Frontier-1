# Compact Output Protocol

🔴 **Chat vs. Markdown Studio Rule (NON-NEGOTIABLE):** Never write architectural plans, decision
tables, implementation reports, or multi-section summaries in chat. Write them into
`Documentation/Plans/*.md` (and/or `Documentation/Plans/SessionNotes.md`), rebuild
`Documentation/Plans/MarkdownStudioViewer.html` (`python3 Automation/BuildMarkdownViewer.py`), and
keep chat replies to **1–3 lines** pointing to the Markdown Studio document and status.

Write for a skimmer, not a reader. **No essay explanations** of code or architecture — the user
will not read them.

**Order:** TL;DR first (1–3 sentences) → table → chart/diagram → bullets → short paragraph (last
resort, ≤4 sentences). Never describe in words what a table, diagram, or chart can show.

🔴 **Decisions (MANDATORY, AUTOMATIC — never skip, never wait to be asked):** the moment you are
comparing options or asking the user to choose, you MUST build the comparison table FIRST
(rank/rating markers per `SKILL-Emoji.md`), THEN state the recommendation or ask the question
directly beneath it. Do this unprompted — the user should never have to ask for the table or
request it after the fact. Asking a choice in prose without the table first is a rule violation.

**Depth scales to the ask:** a definition gets sentences; a full analysis gets tables + charts +
diagrams, 2–3 pages max. Be direct; cut filler, preambles, and hedging.

🔴 **Emoji use is governed solely by `SKILL-Emoji.md`** — only that set is permitted anywhere in
chat, source files, `.slang` shaders, or Markdown documents.
