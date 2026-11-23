# Project Zero content

This folder holds only content this project adds itself. Shared engine content
(fonts, icons) is **referenced, not copied**: at run time the host resolves a
content-relative path here first, then against the shared `EngineContent/`
tree, so the project starts with the full shared font/UI set available and
anything placed here shadows the shared copy (drop your own typeface into
`Fonts/` to override the archive one).

Engine content is copied next to the executable only when it is used: list
the EngineContent-relative paths in `../ContentManifest.toml` and the build
stages exactly those (see `../Build.bat`). An empty manifest stages nothing.
