#!/usr/bin/env python3
"""Live HTTP preview server for Frontier Markdown Studio (Plans, Documentation & Session Notepad).
Binds to 0.0.0.0 so Arena Live Preview proxies it directly into the browser."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys
import urllib.parse
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.resolve()
PLANS_ROOT = (REPO_ROOT / "Documentation" / "Plans").resolve()
DEFAULT_NOTE_REL = "Documentation/Plans/SessionNotes.md"

if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import BuildMarkdownViewer as builder  # noqa: E402


def resolve_safe_note_path(rel_or_name: str) -> pathlib.Path:
    cleaned = (rel_or_name or DEFAULT_NOTE_REL).strip().lstrip("/")
    if not cleaned.startswith("Documentation/Plans/"):
        safe_leaf = re.sub(r"[^A-Za-z0-9._ -]+", "", pathlib.Path(cleaned).name).strip() or "SessionNotes.md"
        if not safe_leaf.lower().endswith(".md"):
            safe_leaf += ".md"
        cleaned = f"Documentation/Plans/{safe_leaf}"
    elif not cleaned.lower().endswith(".md"):
        cleaned += ".md"

    target = (REPO_ROOT / cleaned).resolve()
    target.relative_to(PLANS_ROOT)
    return target


class MarkdownStudioHandler(BaseHTTPRequestHandler):
    def _send_bytes(self, status: int, content_type: str, body: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store, max-age=0")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, status: int, data: object) -> None:
        raw = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self._send_bytes(status, "application/json; charset=utf-8", raw)

    def _read_json_body(self) -> dict[str, object]:
        length = int(self.headers.get("Content-Length") or "0")
        if length <= 0 or length > 8 * 1024 * 1024:
            return {}
        raw = self.rfile.read(length)
        return json.loads(raw.decode("utf-8"))

    def do_OPTIONS(self) -> None:
        self._send_bytes(HTTPStatus.NO_CONTENT, "text/plain; charset=utf-8", b"")

    def do_GET(self) -> None:
        parsed = urllib.parse.urlsplit(self.path)
        route = parsed.path

        if route in ("/", "/index.html", "/MarkdownStudioViewer.html"):
            html_bytes = builder.render_viewer_html().encode("utf-8")
            self._send_bytes(HTTPStatus.OK, "text/html; charset=utf-8", html_bytes)
            return

        if route == "/api/tree":
            files = builder.collect_markdown_documents(include_content=True)
            self._send_json(HTTPStatus.OK, {"root": "Frontier", "files": files})
            return

        if route == "/api/notes":
            qs = urllib.parse.parse_qs(parsed.query)
            requested = (qs.get("path") or [DEFAULT_NOTE_REL])[0]
            try:
                note_file = resolve_safe_note_path(requested)
            except ValueError:
                self._send_json(HTTPStatus.FORBIDDEN, {"error": "Invalid note path"})
                return
            if not note_file.exists():
                note_file.parent.mkdir(parents=True, exist_ok=True)
                note_file.write_text("# Session Notes\n\n", encoding="utf-8")
            stat = note_file.stat()
            self._send_json(
                HTTPStatus.OK,
                {
                    "path": note_file.relative_to(REPO_ROOT).as_posix(),
                    "name": note_file.name,
                    "size": stat.st_size,
                    "mtime": round(stat.st_mtime, 3),
                    "content": note_file.read_text(encoding="utf-8"),
                },
            )
            return

        if route == "/api/file":
            qs = urllib.parse.parse_qs(parsed.query)
            rel_path = (qs.get("path") or [""])[0].strip().lstrip("/")
            if not rel_path:
                self._send_json(HTTPStatus.BAD_REQUEST, {"error": "Missing path query parameter"})
                return

            candidate = (REPO_ROOT / rel_path).resolve()
            try:
                candidate.relative_to(REPO_ROOT)
            except ValueError:
                self._send_json(HTTPStatus.FORBIDDEN, {"error": "Path outside repository"})
                return

            if not candidate.is_file() or candidate.suffix.lower() not in (".md", ".markdown", ".txt"):
                self._send_json(HTTPStatus.NOT_FOUND, {"error": "Markdown file not found"})
                return

            stat = candidate.stat()
            self._send_json(
                HTTPStatus.OK,
                {
                    "path": candidate.relative_to(REPO_ROOT).as_posix(),
                    "name": candidate.name,
                    "size": stat.st_size,
                    "mtime": round(stat.st_mtime, 3),
                    "content": candidate.read_text(encoding="utf-8"),
                },
            )
            return

        if route.startswith("/fonts/"):
            rel_font = route[len("/fonts/"):].lstrip("/")
            fonts_root = (REPO_ROOT / "EngineContent" / "FontsArchives").resolve()
            font_file = (fonts_root / rel_font).resolve()
            try:
                font_file.relative_to(fonts_root)
            except ValueError:
                self._send_json(HTTPStatus.FORBIDDEN, {"error": "Invalid font path"})
                return
            if font_file.is_file() and font_file.suffix.lower() in (".ttf", ".otf"):
                mime = "font/ttf" if font_file.suffix.lower() == ".ttf" else "font/otf"
                self._send_bytes(HTTPStatus.OK, mime, font_file.read_bytes())
                return
            self._send_json(HTTPStatus.NOT_FOUND, {"error": "Font not found"})
            return

        if route == "/favicon.ico":
            self._send_bytes(HTTPStatus.NO_CONTENT, "image/x-icon", b"")
            return

        self._send_json(HTTPStatus.NOT_FOUND, {"error": f"Unknown route: {route}"})

    def do_POST(self) -> None:
        parsed = urllib.parse.urlsplit(self.path)
        route = parsed.path

        if route == "/api/notes/save":
            try:
                payload = self._read_json_body()
                note_file = resolve_safe_note_path(str(payload.get("path") or DEFAULT_NOTE_REL))
                content = str(payload.get("content") if payload.get("content") is not None else "")
                note_file.parent.mkdir(parents=True, exist_ok=True)
                note_file.write_text(content, encoding="utf-8")
                stat = note_file.stat()
                self._send_json(
                    HTTPStatus.OK,
                    {
                        "ok": True,
                        "path": note_file.relative_to(REPO_ROOT).as_posix(),
                        "name": note_file.name,
                        "size": stat.st_size,
                        "mtime": round(stat.st_mtime, 3),
                    },
                )
            except Exception as exc:  # noqa: BLE001
                self._send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": str(exc)})
            return

        if route == "/api/notes/push":
            try:
                payload = self._read_json_body()
                note_file = resolve_safe_note_path(str(payload.get("path") or DEFAULT_NOTE_REL))
                if payload.get("content") is not None:
                    note_file.parent.mkdir(parents=True, exist_ok=True)
                    note_file.write_text(str(payload.get("content")), encoding="utf-8")

                rel_note = note_file.relative_to(REPO_ROOT).as_posix()
                branch_proc = subprocess.run(
                    ["git", "rev-parse", "--abbrev-ref", "HEAD"],
                    cwd=REPO_ROOT,
                    capture_output=True,
                    text=True,
                    check=True,
                )
                branch = branch_proc.stdout.strip()

                subprocess.run(["git", "add", rel_note], cwd=REPO_ROOT, check=True)
                diff_proc = subprocess.run(["git", "diff", "--cached", "--quiet", "--", rel_note], cwd=REPO_ROOT)
                if diff_proc.returncode != 0:
                    subprocess.run(
                        ["git", "commit", "-m", f"Save {note_file.name} from Markdown Studio Notepad"],
                        cwd=REPO_ROOT,
                        capture_output=True,
                        text=True,
                        check=True,
                    )

                push_proc = subprocess.run(
                    ["git", "push", "origin", branch],
                    cwd=REPO_ROOT,
                    capture_output=True,
                    text=True,
                    timeout=30,
                )
                if push_proc.returncode != 0:
                    self._send_json(
                        HTTPStatus.BAD_GATEWAY,
                        {"ok": False, "error": (push_proc.stderr or push_proc.stdout).strip()},
                    )
                    return

                sha_proc = subprocess.run(
                    ["git", "rev-parse", "--short", "HEAD"],
                    cwd=REPO_ROOT,
                    capture_output=True,
                    text=True,
                    check=True,
                )
                self._send_json(
                    HTTPStatus.OK,
                    {
                        "ok": True,
                        "path": rel_note,
                        "branch": branch,
                        "commit": sha_proc.stdout.strip(),
                    },
                )
            except Exception as exc:  # noqa: BLE001
                self._send_json(HTTPStatus.INTERNAL_SERVER_ERROR, {"ok": False, "error": str(exc)})
            return

        self._send_json(HTTPStatus.NOT_FOUND, {"error": f"Unknown POST route: {route}"})

    def log_message(self, fmt: str, *args: object) -> None:
        sys.stdout.write("[MarkdownStudio] " + (fmt % args) + "\n")
        sys.stdout.flush()


def main() -> None:
    parser = argparse.ArgumentParser(description="Serve Frontier Markdown Studio")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), MarkdownStudioHandler)
    print(f"[MarkdownStudio] Serving live .md browser on http://{args.host}:{args.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
