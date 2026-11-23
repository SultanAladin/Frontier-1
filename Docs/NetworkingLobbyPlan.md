# Lobby, match and voice extension

## Observed milestone
The owner's latest screenshot shows real `LOGIN_VERIFIED auth=success
connect=success`, a successful friends query, and loaded profile details. This is
user-PC evidence, not an authentication performed by CI. Personal profile values
and the supplied client secret are deliberately not recorded here.

## Located UI
The requested Lobby implementation is `app/lobby.html` in
`streamlinkinbox/Frontier`, branch `arena/01a06c6c-frontier`, commit
`1f2126870aee94c57c32631ccd8ad0cefa461de1`.
It has a lobby hero/navigation, player-card grid, voice strip, chat and right-side
match panel. A reference copy and provenance are under Project-Networking/Reference.
Its EOS-labelled JavaScript is simulated; only visual structure is reusable.

Latest previously used Slate branch was checked through GitHub:
`unassignedinbox/Slate`, `arena/01a0fd48-slate`,
`bf0bdd5005f97b6375c21bc74e9a4bd8b41417cd`.

## Requested flow / pending implementation
1. Successful Auth + Connect creates an EOS lobby automatically (once per login).
2. Display actual local player and distinctly labelled local dummy players for
   solo readiness testing. Dummy players are not Epic accounts/PUIDs, network
   members or RTC participants; exclude them from competitive/cloud statistics.
3. Publish readiness for real players through lobby member attributes; handle
   membership changes and clear readiness as appropriate.
4. Host creates/registers/starts a real EOS match session only after readiness and
   successful callbacks. Serialize operations to prevent duplicate create/start.
   The match session is a separate EOS Sessions object, not the lobby itself.
   Members join it explicitly to get a local session; the lobby only carries
   session_id/match_state hints.
5. EOS lobby RTC audio, initially muted; explicit microphone control, real RTC
   connection state and errors. Dummy players cannot prove voice transmission.
6. Leaving the lobby always closes the match session first: host ends,
   unregisters and destroys; members destroy their local joined session, then
   the lobby is left/destroyed. Kicks and disconnects trigger the same cascade.
7. Browse both EOS lobbies and EOS match sessions with text filters, join by
   list or by lobby ID. Session search results keep their details handles for
   Join; lobby joins use Join-by-ID.
8. Host-editable lobby settings (bucket, max members, permission, presence,
   invites, RTC, host migration, join-by-ID, rejoin rule, map/mode/region/note)
   applied on create and live; plus match-session settings (name, bucket, max
   players, permission, join-in-progress, presence, map/mode) used on prepare.
9. Persist last successful login UTC, last lobby/session ID, session start/end UTC,
   duration and completed/abandoned test matches. Never persist access tokens or
   fabricate gameplay metrics, scores or kills when no gameplay occurred.
10. Storage scope (local or cross-PC EOS cloud) and portable credential format need
   confirmation. Client-written records are not authoritative competitive stats.

## Changes already made this turn
- Replaced Save log UI action with Copy log and explicit Win32 clipboard error
  handling. Source change only; Windows build not run yet this turn.
- Located and copied the exact visual reference with its simulation caveat.

## Implemented this turn
- Real EOS lobby creation with RTC room enabled and local audio input muted, plus
  host-owned session preparation, registration of only real members, start, end
  and destroy. Automatic start requires real and dummy readiness, and is checked
  against membership changes after registration.
- True match-session split: the Sessions object is created/registered/started/
  ended/destroyed separately from the lobby. Members explicitly join the host
  session by backend session ID to get a local session; RegisterPlayers policy
  failures (EOS_ClientPolicyMissingAction) are surfaced with Dev Portal guidance
  and allow a lifecycle-only start without registration.
- Lobby-exit cascade: leaving always ends (host), unregisters, and destroys the
  match session before leaving/destroying the lobby. Kicks, closes and disconnects
  trigger the same path. Stale registrations are pruned when members depart.
- Lobby + session browsers with client-side filters, join-by-list and join-by-ID.
  Host-editable lobby settings (bucket, members, permission, presence, invites,
  RTC, migration, join-by-ID, rejoin rule, map/mode/region/note) applied on create
  and live; session settings (name, bucket, players, permission, join-in-progress,
  presence, map/mode) used on prepare. All validated deterministically.
- Clean tabbed UI (Lobby / Browse / Settings / History) in the reference blue,
  black, white and dark-grey palette. Voice status is acknowledged only from SDK
  callbacks, never on button click.
- EOS Player Data Storage history: immutable per-event files, strict parser,
  bounded local cache, explicit pending markers, retry, and no claim of cloud
  success on failure. Records contain login UTC, session IDs, duration and
  explicit dummy counts, never invented gameplay metrics.
- Private portable `Charge.local.ini` (client id, secret, 64-hex data key) read
  from beside the executable, with strict parsing, no truncation, buffer wiping,
  Git-ignored local archive, and a tracked-source scan for private values.
- Epic auto-login: desktop PersistentAuth is tried first (no browser), with a
  one-time Account Portal fallback. The portal login stores the token the next
  auto-login uses. Setup offers an auto-login toggle, a persisted app preference
  file, and forget/revoke support (revoked immediately when logged in, otherwise
  on the next login).
- App UI language: English plus Simplified Chinese, switchable live in Setup
  (CJK glyphs are merged into the UI font at startup, no restart needed).
  Diagnostics and logs intentionally stay English for support.
- Tag discovery: lobbies publish style (ranked/casual) and language tags plus
  map/mode/region/match-state. The Browse tab filters server-side by tag and
  client-side by status (open / in-match / full) and visibility (public /
  presence / invite). Join-by-ID remains as a collapsed fallback.
## Full multiplayer: remaining EOS work
Lobby, session and voice-room plumbing exist, but no game is networked yet.
Roughly, in dependency order:
1. Gameplay transport: EOS P2P (NAT punch-through/relay, connection requests,
   packet send/receive) or custom sockets seeded from the session host address.
   Nothing moves game state today.
2. Replication: host-authoritative snapshots, client interpolation, ownership
   and input messages on top of the transport.
3. Invites and presence joins: lobby/session SendInvite, invite notifications,
   and join-via-presence handling.
4. Session depth: join-in-progress policy per game mode, host migration (or
   explicit host transfer), backfill, and ranked bucket queues.
5. Stats, leaderboards, achievements: EOS Stats/Leaderboards/Achievements for
   anything the ranked tag promises.
6. Anti-cheat for public ranked: EOS AntiCheatClient/Server integration.
7. Compliance and safety: Sanctions, age gates (KWS), player reports.
8. Dedicated servers (optional): session registration/management from a server
   build instead of a player host.

## Verified vs unverified
Deterministic local logic (readiness, history parsing/bounds, portable config,
lobby/session settings validation, search filters, tag derivation/matching,
app-settings parsing, UI translation fallback) is covered by
VisualProof/Networking/LobbyChecks.cpp. Windows builds, real SDK platform
startup, and extracted native UI rendering passed in CI.
**Not verified:** authenticated lobby creation, lobby/session search results,
member session joins, RTC audio transmission, session start/end with a real
second player, and actual cloud read/write. Gameplay transport/replication and
authoritative competitive statistics are not implemented.

No EOS lobby/session/RTC/data-storage success is claimed by this document.
No secret has been embedded in source, reference files or a public artifact.
