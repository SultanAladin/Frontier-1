#!/usr/bin/env python3
"""Download Slate fonts by fetching individual files from GitHub's API.

This is an alternative to DownloadFonts.py which downloads full zip archives.
Use this when zip downloads fail (SSL errors, timeouts) but the GitHub API is reachable.
The script is safe to run repeatedly. Already-present files are skipped.
"""
from pathlib import Path
import json, os, ssl, urllib.request

ROOT = Path(__file__).resolve().parents[1] / "EngineContent" / "FontArchives"

# (family, repo, branch, list of file paths inside the repo)
FONTS = [
    ("OpenSans", "googlefonts/opensans", "main", [
        "fonts/noto-set/ttf/OpenSans-Regular.ttf",
        "fonts/noto-set/ttf/OpenSans-Bold.ttf",
        "fonts/noto-set/ttf/OpenSans-Italic.ttf",
        "fonts/noto-set/ttf/OpenSans-BoldItalic.ttf",
        "fonts/noto-set/ttf/OpenSans-Light.ttf",
        "fonts/noto-set/ttf/OpenSans-SemiBold.ttf",
        "fonts/noto-set/ttf/OpenSans-ExtraBold.ttf",
    ]),
    ("Archivo", "Omnibus-Type/Archivo", "master", [
        "fonts/otf/Archivo-Regular.otf",
        "fonts/otf/Archivo-Bold.otf",
        "fonts/otf/Archivo-Italic.otf",
        "fonts/otf/Archivo-Black.otf",
        "fonts/otf/Archivo-Light.otf",
        "fonts/otf/Archivo-Medium.otf",
        "fonts/otf/Archivo-SemiBold.otf",
    ]),
    ("Inter", "rsms/inter", "v3.19", [
        "docs/font-files/Inter-Thin.otf",
        "docs/font-files/Inter-ExtraLight.otf",
        "docs/font-files/Inter-Light.otf",
        "docs/font-files/Inter-Regular.otf",
        "docs/font-files/Inter-Medium.otf",
        "docs/font-files/Inter-SemiBold.otf",
        "docs/font-files/Inter-Bold.otf",
        "docs/font-files/Inter-ExtraBold.otf",
        "docs/font-files/Inter-Black.otf",
    ]),
    ("JetBrainsMono", "JetBrains/JetBrainsMono", "master", [
        "fonts/archives/otf/JetBrainsMono-Regular.otf",
        "fonts/archives/otf/JetBrainsMono-Bold.otf",
        "fonts/archives/otf/JetBrainsMono-Italic.otf",
        "fonts/archives/otf/JetBrainsMono-Light.otf",
        "fonts/archives/otf/JetBrainsMono-Medium.otf",
        "fonts/archives/otf/JetBrainsMono-ExtraLight.otf",
        "fonts/archives/otf/JetBrainsMono-ExtraBold.otf",
        "fonts/archives/otf/JetBrainsMono-SemiBold.otf",
        "fonts/archives/otf/JetBrainsMono-Thin.otf",
    ]),
]


def fetch_raw(repo, branch, path):
    url = f"https://raw.githubusercontent.com/{repo}/{branch}/{path}"
    ctx = ssl.create_default_context()
    req = urllib.request.Request(url, headers={"User-Agent": "Slate-FontFetcher/1.0"})
    with urllib.request.urlopen(req, timeout=60, context=ctx) as resp:
        return resp.read()


downloaded = 0
skipped = 0
failed = 0

for family, repo, branch, paths in FONTS:
    dest = ROOT / family
    dest.mkdir(parents=True, exist_ok=True)
    for path in paths:
        filename = os.path.basename(path)
        target = dest / filename
        if target.exists() and target.stat().st_size > 1000:
            skipped += 1
            continue
        try:
            print(f"  {filename}...", end=" ", flush=True)
            data = fetch_raw(repo, branch, path)
            target.write_bytes(data)
            print(f"OK ({len(data) // 1024} KB)")
            downloaded += 1
        except urllib.error.HTTPError:
            # Try alternate branch names
            for alt_branch in ([], ["main"] if branch != "main" else ["master"]):
                pass
            print(f"FAILED: 404")
            failed += 1
        except Exception as e:
            print(f"FAILED: {e}")
            failed += 1
        if target.exists() and target.stat().st_size < 1000:
            target.unlink()
            failed += 1
            downloaded -= 1

    (dest / "SOURCE.txt").write_text(
        f"Family: {family}\nSource: see upstream repository licenses.\n", encoding="utf-8"
    )

print(f"\nDone: {downloaded} downloaded, {skipped} skipped, {failed} failed")
print(f"Fonts under: {ROOT}")
