#!/usr/bin/env python3
"""Download Slate's open-source fonts with network and SSL fallbacks.

The command is safe to run repeatedly. Failed families are reported and skipped so one bad
upstream does not prevent the remaining families from being installed.
"""
from pathlib import Path
from urllib.request import Request, urlopen
from urllib.error import HTTPError, URLError
from zipfile import ZipFile, BadZipFile
from io import BytesIO
import argparse, shutil, ssl, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1] / "EngineContent" / "FontArchives"
FONTS = {
    "OpenSans": [
        ("https://github.com/googlefonts/opensans/archive/refs/heads/main.zip", "opensans-main"),
        ("https://codeload.github.com/googlefonts/opensans/zip/refs/heads/main", "opensans-main"),
    ],
    "Archivo": [
        ("https://github.com/Omnibus-Type/Archivo/archive/refs/heads/master.zip", "Archivo-master"),
        ("https://codeload.github.com/Omnibus-Type/Archivo/zip/refs/heads/master", "Archivo-master"),
    ],
    "Inter": [
        # 📝 The master archive carries only InterVariable.ttf, and a variable font loads one Regular face
        #    in Slate's stb-based atlas — so the weight strips would show a single "Regular" tile. v3.19's
        #    docs/font-files carries the nine static upright faces; those are what the strips enumerate.
        ("https://github.com/rsms/inter/archive/refs/tags/v3.19.zip", "inter-3.19",
         ["Inter-Thin.otf", "Inter-ExtraLight.otf", "Inter-Light.otf", "Inter-Regular.otf",
          "Inter-Medium.otf", "Inter-SemiBold.otf", "Inter-Bold.otf", "Inter-ExtraBold.otf",
          "Inter-Black.otf"]),
        ("https://codeload.github.com/rsms/inter/zip/refs/tags/v3.19", "inter-3.19",
         ["Inter-Thin.otf", "Inter-ExtraLight.otf", "Inter-Light.otf", "Inter-Regular.otf",
          "Inter-Medium.otf", "Inter-SemiBold.otf", "Inter-Bold.otf", "Inter-ExtraBold.otf",
          "Inter-Black.otf"]),
    ],
    "JetBrainsMono": [
        ("https://github.com/JetBrains/JetBrainsMono/archive/refs/heads/master.zip", "JetBrainsMono-master"),
        ("https://codeload.github.com/JetBrains/JetBrainsMono/zip/refs/heads/master", "JetBrainsMono-master"),
    ],
}


def fetch(url):
    request = Request(url, headers={"User-Agent": "Slate-FontFetcher/1.0", "Accept": "application/octet-stream"})
    errors = []
    for context in (None, ssl._create_unverified_context()):
        try:
            with urlopen(request, timeout=90, context=context) as response:
                data = response.read()
                if data.startswith(b"PK"):
                    return data
                errors.append(f"{url}: response was not a zip archive")
        except Exception as error:
            errors.append(f"{url}: {error}")

    # Windows and corporate environments often have curl configured even when Python SSL is not.
    curl = shutil.which("curl")
    if curl:
        for insecure in ([], ["-k"]):
            try:
                result = subprocess.run([curl, "-L", "--fail", "--silent", "--show-error", *insecure, url],
                                        capture_output=True, timeout=120)
                if result.returncode == 0 and result.stdout.startswith(b"PK"):
                    return result.stdout
                errors.append(f"curl {url}: exit {result.returncode}")
            except Exception as error:
                errors.append(f"curl {url}: {error}")

    # PowerShell is the final Windows fallback when curl is absent or unavailable.
    powershell = shutil.which("powershell") or shutil.which("pwsh")
    if powershell:
        try:
            with tempfile.NamedTemporaryFile(suffix=".zip", delete=False) as temporary:
                target = temporary.name
            command = f"Invoke-WebRequest -UseBasicParsing -Uri '{url}' -OutFile '{target}'"
            subprocess.run([powershell, "-NoProfile", "-NonInteractive", "-Command", command],
                           check=True, timeout=180, capture_output=True)
            data = Path(target).read_bytes()
            Path(target).unlink(missing_ok=True)
            if data.startswith(b"PK"):
                return data
        except Exception as error:
            errors.append(f"PowerShell {url}: {error}")

    raise RuntimeError("; ".join(errors[-4:]))


def download(name, candidates):
    destination = ROOT / name
    destination.mkdir(parents=True, exist_ok=True)
    if list(destination.glob("*.ttf")) or list(destination.glob("*.otf")):
        print(f"[skip] {name}: font files already exist")
        return True

    for candidate in candidates:
        url, archive_root = candidate[0], candidate[1]
        keep = candidate[2] if len(candidate) > 2 else None
        print(f"[try] {name}: {url}")
        try:
            archive = ZipFile(BytesIO(fetch(url)))
            entries = [entry for entry in archive.namelist()
                       if entry.startswith(archive_root + "/") and entry.lower().endswith((".ttf", ".otf"))]
            if not entries:
                raise RuntimeError("archive contains no font files")
            copied = 0
            for entry in entries:
                path = Path(entry)
                if "fonts" not in path.parts and "static" not in path.parts and "font-files" not in path.parts:
                    continue
                if keep is not None and path.name not in keep:
                    continue
                target = destination / path.name
                with archive.open(entry) as source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)
                copied += 1
            if copied == 0:
                raise RuntimeError("archive contains no usable static font files")
            (destination / "SOURCE.txt").write_text(
                f"Source: {url}\nLicense: see the upstream repository license.\n", encoding="utf-8")
            print(f"[ok] {name}: installed {copied} face files")
            return True
        except (HTTPError, URLError, BadZipFile, OSError, RuntimeError) as error:
            print(f"[retry] {name}: {error}")

    print(f"[warning] {name}: unavailable; Slate will use its built-in font fallback")
    return False


parser = argparse.ArgumentParser()
parser.add_argument("--family", action="append", choices=sorted(FONTS), help="download only this family")
args = parser.parse_args()
ROOT.mkdir(parents=True, exist_ok=True)
selected = args.family or list(FONTS)
failed = [name for name in selected if not download(name, FONTS[name])]
print(f"Fonts are available under {ROOT}")
if failed:
    print("Unavailable families: " + ", ".join(failed))
