# Frontier workspace notes

## Source checkout

`FrontierSource/` is the pinned submodule for `egoistinbox-hue/Frontier`, branch `arena/f490d972-frontier`. The live HTML Project Browser is at `Experimental/Project browser/` in this workspace; it is a configuration prototype, not the native EOS/Photon runtime.

## SDK source links (private Drive; manual authorized access)

- [EOS Flattened SDK folder](https://drive.google.com/drive/folders/1GvNZdsPDj26vF57-h9pHdc6sGJI2BZPb) — contains `Bin/`, `Include/`, and `Lib/`.
- [EOS SDK 1.19.2.1 release archive](https://drive.google.com/file/d/1vEc4b4I0Bsmvuhz8LJoWAXNdAvPfbZZA/view?usp=drivesdk) — large archive; use only with an authorized Google account. The link opens the Drive file; it is not a public anonymous download URL.
- Photon Realtime Core 6.0.0-preview-347 is in the user-provided Drive folder; the user also uploaded `photon-realtime-core-6.0.0-preview-347-20261126T033842Z-1-001.zip` (193,488,030 bytes) to My Drive. The Drive connector refused to transfer it into this workspace because its download ceiling is 100 MiB. Do not claim it is locally staged or buildable until a smaller/platform-specific archive is made available. Do not silently change SDK versions. Jolt 5.6.0 is already hash-pinned in `FrontierSource/Automation/Dependencies.json` and is fetched by `python3 FrontierSource/Automation/PrepareDependencies.py` from the upstream source archive.

The Drive links are private references, not anonymous download endpoints. Do not change sharing permissions or publish proprietary SDK binaries without explicit approval and license review. CI/build scripts cannot use a user's Drive OAuth session; any automated download needs an approved private-secret mechanism. Keep vendor SDK payloads out of Git and out of distributable client packages unless the vendor license explicitly allows redistribution.

## Project settings and credentials

The HTML Project Details page exports `ProjectSettings.toml` (public project metadata only) or, after a separate warning/confirmation, `ProjectSettings.local.toml` (plaintext local test secrets). The latter is ignored by Git and must never be committed, shared, or included in a product package. Use **Open TOML** to reload a saved config; secrets are kept in page memory unless explicitly exported to the local-only file. EOS player tokens are short-lived and must not be written to TOML. Photon player authentication is intended to use the EOS identity token; never ship server-side client secrets in a game client.

The selected engine source currently exposes EOS/Photon transport seams and stub selection. The older `unassignedinbox/Frontier-OLD` project contains the fuller EOS integration and is the reference for the C++ follow-up. Do not present the HTML controls as proof of a live SDK connection.
