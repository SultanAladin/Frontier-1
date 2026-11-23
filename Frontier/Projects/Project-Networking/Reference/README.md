# Existing Lobby UI reference

Copied at the owner's request from:
https://github.com/streamlinkinbox/Frontier/blob/1f2126870aee94c57c32631ccd8ad0cefa461de1/app/lobby.html

Branch: arena/01a06c6c-frontier. This file is a **visual reference**, not the
networking implementation. Its JavaScript simulates EOS results, members, chat,
voice and matches. Do not ship those success messages as real SDK results.
Its external font and other page assets are not included here.

Adapt its lobby header, player-card grid, voice panel and right-hand match panel
into the existing native ImGui/GLFW host. Keep real EOS operations separate from
explicitly labelled local dummy-player test controls.
