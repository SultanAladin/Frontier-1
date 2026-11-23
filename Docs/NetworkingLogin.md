## Photon transport slice (current)

After EOS login the app now selects a multiplayer transport and shows it on the
account card. Standard accounts stay on the **EOS direct path** (lobby + voice).
The manual **Premium multiplayer (Photon)** switch in Setup routes match traffic
over **Photon Realtime Core C++** (never Fusion): Epic-auth connect with the EOS
ID token, join-or-create `FrontierDevRoom`, first handshake packet, then engine
replication packets. Photon failures fall back to EOS automatically; Ecom
catalog-ownership tokens stay dormant until `EOS_CATALOG_ITEM_IDS` is set. No
tokens are written to logs. Premium is a manual toggle for now; Xsolla
entitlement will drive it later through `ResolvePremiumAccess()`. The Xsolla
slice is a REST client (`Source/XsollaStore.*`): **no vendor binaries to
download or pin**. The C++ SDK zip is unpublished; the public C# zip is unused.

Build status: CI links the official Photon Realtime Core 6.0.0.347 **/MD x64**
archives (`Common-cpp`, `Photon-cpp`, `LoadBalancing-cpp` release_md, hash-pinned
in `Build/WindowsPhotonManifest.json` from the owner's Drive upload) against
pinned headers (`defold/extension-photon-realtime @ 8dad867470f2eb75aeae0c437e10ee723d8d1ccf`,
SDK 5.0.14.3). The link succeeds with no unresolved symbols. The mirror's own
Win64 libs are `/MT`-only and can never link into this `/MD` build. Realtime AppId `08ec2e74-b472-42d4-942e-27894128e8fb`;
dashboard Epic provider `clientid=xyza7891AKjtZj8wTzcmI5F3oc1zLU4s`,
`catalogitemids=none` (no ownership check yet). Developer overrides:
`PHOTON_APP_ID`, `PHOTON_ROOM`, `PHOTON_APP_VERSION`,
`FRONTIER_PREMIUM_MULTIPLAYER=1`.

## Lobby build

**Latest Windows download:** [Project-Networking-Windows-x64.zip](https://github.com/c7egoist/Frontier/releases/download/networking-test-37649286520/Project-Networking-Windows-x64.zip)
(run [37649286520](https://github.com/c7egoist/Frontier/actions/runs/37649286520), source `f2f5a680c456f6b2f1fc26d5d1bcd71a7522e11b`, 20,179,272 bytes with `.sha256` sidecar).

Extract into a fresh folder and open **Charge.exe**. Put the **private**
`Charge.local.ini` next to it for portable login and a stable cross-PC EOS cloud
data key. That file is intentionally excluded from the public download.

This build splits the match session from the lobby (members join it explicitly),
closes the session before leaving the lobby, adds lobby + session browsers and
host-editable lobby/session settings, and replaces the UI with clean
Lobby/Browse/Settings/History tabs. If the log shows
`session_register_real_players result=EOS_ClientPolicyMissingAction`, grant the
Sessions actions to the Dev client policy in the Epic portal; an unregistered
lifecycle-only start remains available. This build also adds Epic auto-login
(saved token, portal fallback), an English / Simplified-Chinese UI toggle, and
tag-based lobby discovery (style, language, status, visibility). It adds the
Photon transport slice: EOS/Photon routing after login with a manual premium
switch, Epic-auth Photon transport linked against Realtime Core 6.0.0.347 /MD
x64 libs, dormant Ecom ownership, and redacted transport diagnostics (see above).

Full lobby, voice, session and history notes: `Docs/NetworkingLobbyPlan.md` and
`Frontier/Projects/Project-Networking/Build/BinaryInstructions.txt`.
CI proves the build, deterministic local logic, real SDK/platform startup and
native UI rendering. It does not authenticate a player or prove live lobby,
search, session joins, voice, or cloud success.

## Country permission and signed-in profile

**Latest Windows download:** [Project-Networking-Windows-x64.zip](https://github.com/c7egoist/Frontier/releases/download/networking-test-37598078437/Project-Networking-Windows-x64.zip).
Open **Charge.exe** after extracting into a fresh folder. Existing Windows vault
credentials are reused. Review Epic's updated consent request; no secret re-entry
is required unless the saved credential is invalid or rotated.

[Windows run 37598078437](https://github.com/c7egoist/Frontier/actions/runs/37598078437)
passed for source `4771d2cb8eda6333ac85a95364c23e30cdcac57a` in 3m37s. Checks
cover the four required scopes, explicit creation consent/duplicate approval/
terminal cancellation, signed-out profile guards, SDK/platform creation, native
GUI rendering and extracted packaging. Real profile values and successful player
Auth/Connect were not exercised by these tests. Latest Slate fetched for this
change: `bf0bdd5005f97b6375c21bc74e9a4bd8b41417cd`.

The user's screenshots now demonstrate actual Epic Account Portal rendering and
consent UI. They also show Epic's **Missing Permissions** screen: Country was
required by Charge but not requested. A later `EOS_Canceled` result is not an
Auth/Connect success. Earlier saved-credential/platform-creation blockers have
been passed in the newer screenshot.

The authorization request now includes Basic Profile, Friends List, Presence and
Country, matching Charge's configured required permissions. Setup's optional
friends control now controls **loading friends after login**, not omission of a
required consent scope. No email or friend-management scope is requested.

After Auth AND Connect succeed, the app queries EOS UserInfo for the selected
account, using the local authenticated account as the query caller. The signed-in
card displays Epic's display name, country and preferred language where available,
and separate Auth/Connect verification. Missing fields say unavailable; nothing is
inferred from location metadata or invented. Profile values remain in memory,
are cleared on disconnect/session retirement, and are never written to the log or
credential vault. Refreshing the profile does not launch Auth; a profile query
failure does not log the user out.

If Auth succeeds but Connect needs a NEW Dev product user, the app now retains
the platform/continuance token and presents an explicit **Create NEW Dev profile &
continue** action. Decline/cancel if an existing game identity needs linking.
Approval continues Connect without opening Account Portal again. No profile is
created without this action or the existing explicit advanced opt-in. The consent
wait expires after 180 seconds; cancellations and failures never auto-retry Auth.

The unreviewed-brand notice is controlled by Epic's application review and is not
bypassed by these changes. A real completed Auth/Connect session and returned
profile values remain unverified until the user completes the new flow.

## Platform-refusal diagnostic update

**Previous platform-diagnostic download:** [Windows ZIP](https://github.com/c7egoist/Frontier/releases/download/networking-test-37596249555/Project-Networking-Windows-x64.zip).
[Windows run 37596249555](https://github.com/c7egoist/Frontier/actions/runs/37596249555)
passed on source `c7911f83e9c2f1022ff8d1d3838fb25edbe96c9f`, including real
platform creation with a synthetic credential in both the console check and
extracted GUI/OpenGL process. SDK lifetime, input validation and diagnostic
redaction checks passed. Real player authentication was not attempted.


`platform=refused` means EOS_Platform_Create returned null, before Account Portal
is requested. A saved Windows credential can still contain invalid input: saving
is not credential validation. We reproduced this exact failure with an 80-character
synthetic secret. EOS permits at most 64 characters for both client ID and secret.
That establishes a possible cause, **not proof of what was on the user's PC**.

The update validates empty, oversized and whitespace/non-ASCII credentials before
saving or submitting them, without truncating or logging their contents. Focused
credential fields select their entire value, and **Paste secret** replaces rather
than appends. Existing vault entries remain usable, but invalid entries prompt
Setup for correction. Temporary-secret clearing no longer incorrectly asks users
with saved credentials to re-enter them on every retry.

Platform creation now captures SDK warnings/errors into fixed, redacted diagnostic
categories. SDK message bodies, tokens, identifiers and paths are never forwarded
to the UI or log. Unknown SDK messages are withheld, not guessed. SDK callbacks
only update atomic flags; GUI logging stays on the caller thread.

Setup is centred and constrained to the viewport, with a scrolling fields region
and reserved space for Apply / Save & log in / Cancel. Technical settings still
remain hidden behind Setup.

`LoginHost --platform-check` now creates/releases the real platform with a clearly
synthetic credential, without calling Auth or Connect. CI also exercises platform
creation with the GUI's OpenGL context and checks credential bounds and diagnostic
redaction. These checks are not successful player authentication.

## Minimal login / official launcher update

**Previous Windows download (18,912,276 bytes):**
[Project-Networking-Windows-x64.zip](https://github.com/c7egoist/Frontier/releases/download/networking-test-37594257135/Project-Networking-Windows-x64.zip)
with a SHA-256 sidecar on the [release page](https://github.com/c7egoist/Frontier/releases/tag/networking-test-37594257135).

[Windows run 37594257135](https://github.com/c7egoist/Frontier/actions/runs/37594257135)
passed against source `f01d15dae7e2bc5cab0dd8b2d7d63bf7c295b43d`:
MSVC build, SDK lifetime/refusal checks, ImGui rendering, vendor-generated launcher
configuration and exact Bootstrapper hash verification, and extracted child-app
startup. The vendor launcher uses the Windows GUI PE subsystem (no console).
Latest Slate was re-fetched: `bf0bdd5005f97b6375c21bc74e9a4bd8b41417cd`.
Windows Credential Manager persistence, actual bootstrapper/service startup, and
real Auth/Connect/overlay interaction were not exercised by this CI smoke test.
The Linux lifecycle/ABI/refusal checks were rerun on the existing unchanged EOS
backend build; they passed and did not attempt authentication.

The new package entry point is **Charge.exe**, generated by the hash-pinned Epic
EOSBootstrapperTool. Its vendor-generated configuration launches
`App/NetworkingLogin.exe` with working directory `App`; the EOS runtime is beside
that application. Do not launch the child directly. EpicOnlineServicesInstaller.exe
is a separate, one-time prerequisite from the already-uploaded EOS Tools folder;
no installer is silently run, embedded in Git, or executed by CI.

The signed-out UI is a dark login card and colour-coded console. **Setup** hides
technical settings. Account Portal is the default. Optional explicit opt-in stores
only the application client ID/secret in Windows Credential Manager under
`Frontier/Charge/Dev/EOSClient`; process buffers are wiped after submission. Use
**Forget saved credential** or Windows Credential Manager to delete it. This
protects storage at rest, not against processes running as the same Windows user.
No account password or player token is persisted.

Epic determines the Account Portal UI. We do not fabricate a browser fallback or
bypass bootstrapper/service prerequisites. Windows CI checks vendor-generated
packaging and direct child rendering, not actual service launch, Epic sign-in,
consent, friends or overlay. A real user login remains unverified.

The earlier release details below describe historical builds and their limitations;
follow START-HERE.txt shipped in the new package for current launch instructions.

# Project-Networking login app

## Windowed release

Extract the release ZIP and double-click **NetworkingLogin.exe** at its root. It is a native
Dear ImGui + GLFW window, linked with the Windows GUI subsystem, so it does not open a console.
The EOS runtime is included beside it. No CMD or PowerShell launcher is needed.

The panel accepts a masked client secret, developer credential name, login method, and optional Client ID override.
It uses explicit in-memory configuration, not process environment variables. ImGui settings/log persistence is
disabled. The client secret is cleared after submission and on exit and is never emitted to diagnostics.
Login/Cancel, SDK checks, activity details, and a native Save Log dialog are available.
Developer Authentication Tool or Account Portal prerequisites below still apply; a GUI does not remove them.

The standalone window is a diagnostic requested by the user, not a second game host. Frontier.exe remains the
engine host; the project DLL remains independently usable. The console executable is retained only for CI checks
and is no longer included in the end-user ZIP. The fixed-function ImGui OpenGL backend is used for this small
diagnostic window. Windows CI uses a hash-pinned Mesa software OpenGL driver because hosted runners
have no hardware OpenGL ICD. Mesa is injected only into CI verification directories, not shipped in the ZIP.
The released app uses the user's installed Windows graphics driver.

Pinned ImGui/GLFW revisions are in `Build/GuiDependencies.json`. CI captures the rendered window in
`WindowProof.bmp`, tests missing-input rejection, and repeats the GUI smoke test from the extracted release ZIP.
Neither that image nor SDK startup is proof of player authentication.


## What is delivered

`Frontier/Projects/Project-Networking` is an overlay for the existing Slate checkout, not a copy of its engine.
`ProjectNetworking.frontier` opens a project-owned empty scene and a revision-3 project DLL. The DLL ticks the
same EOS exchange as the standalone console diagnostic. It does not create another game window or renderer.

The implementation uses your supplied Dev Product/Sandbox/Deployment IDs and original Client ID. Secrets are
read only from the local process environment. If you replaced the entire client, override `EOS_CLIENT_ID`.
The Application ID is not an EOS platform initialization field.

The sequence is real EOS Auth -> copy the selected account's ID token -> EOS Connect -> validate the PUID.
SDK 1.19's SelectedAccountId is used for game-scoped identity, including previously merged Epic accounts.
Optional new-PUID creation requires explicit consent. Failures, missing credentials and timeouts cannot report success.
Raw tokens, secrets, Epic account IDs and PUID strings are not emitted by the application diagnostics.

This remains a development login/social diagnostic, not production session management. It does not implement
token refresh, identity linking, Photon, commerce, lobbies, or multiplayer traffic. Local success is not a substitute
for backend identity verification. The app initializes EOS once and shuts it down only on final application exit. Disconnect/retry retires only
the platform. It refuses an SDK lifetime owned by another subsystem rather than guessing ownership.

## Executed evidence

See `VisualProof/Networking/SdkChecks.log` and `SdkBuildEvidence.json` for actual captured results and hashes.
The older `LocalChecks.log` is the earlier SDK-independent check, not the latest evidence.

- Compiled and linked the console and project shared library against the downloaded EOS SDK using Linux g++.
- Loaded the real EOS runtime; its reported version is `1.19.2.1-58105819`.
- Ran real EOS initialize/shutdown successfully twice, in separate processes.
- Ran missing-secret and invalid-argument refusal checks against the linked console.
- Loaded the real project image and checked ABI rejection, valid entry points, and refused project construction.
- Ran seven synthetic login-order checks. Those are explicitly NOT authentication proof.
- Windows/MSVC build and real SDK smoke checks subsequently passed in Actions run `37583886172`.
- Windows scene opening and LIVE PLAYER AUTHENTICATION remain unverified.

No replacement secret was supplied. This sandbox's outbound allowlist excludes Epic authentication services.
No player login was attempted, and no successful player login is claimed.

## SDK storage and provenance

The private Drive folder is reachable through its direct link:
`https://drive.google.com/drive/folders/1_qBBX5gJTFkkRVOuB7e3Q2TtlM9FR62s`.
Its flattened layout already contains `Include`, `Lib`, and `Bin`; use that folder itself as `SdkRoot`.
Windows x64 and Linux x64 binaries are present. The downloaded Linux runtime and required include closure are
staged under ignored `ThirdParty/EOS`. `SdkManifest.json` records hashes, not proprietary SDK content.
No license/notices file was found at the folder root; retain those from the original Epic package.
The flattened upload does not include the Developer Authentication Tool or Bootstrapper at its root.
Obtain these from your original SDK/tool downloads as needed; they have not been installed by this integration.

`Upstream.json` records the latest inspected Slate revision, `bf0bdd5005f97b6375c21bc74e9a4bd8b41417cd`.
Fetch the upstream branch before subsequent changes and reconcile it; never discard local work with a reset.
Builds include the supplied checkout's real ABI header and assert the supported revision and fingerprint.
For the local checks, that header was extracted from the fetched commit without importing the entire engine.

## Build on Windows

Use x64 Visual Studio developer PowerShell at this Frontier checkout's root:

```powershell
$Project = (Resolve-Path '.\Frontier\Projects\Project-Networking').Path
& "$Project\Build\ToolchainSequence.ps1" `
    -SdkRoot 'C:\Dependencies\EOS Flattened' `
    -SlateRoot 'C:\Source\Slate' `
    -GuiRoot 'C:\Dependencies\NetworkingGui'
```

For manual builds, clone GLFW and ImGui at the revisions in `Build/GuiDependencies.json` into
`NetworkingGui/glfw` and `NetworkingGui/imgui`, respectively. CMake 3.24+ builds the standalone window and its
static GUI libraries; this does not replace Slate's direct MSVC engine build. The `/MD` setting is retained.
The Linux helper optionally accepts `--gui-root` for the same GUI source when OpenGL/window-system headers exist.

Required SDK files include `Include/eos_sdk.h`, `Lib/EOSSDK-Win64-Shipping.lib`, and
`Bin/EOSSDK-Win64-Shipping.dll`. The script uses C++20 and `/MD` for both targets and stages the runtime next
to the console. Both build targets compile the same exchange; no shared-engine source batch needs changing.
Output and build evidence are in `Build/Output`, ignored by Git.

## First real player login: Developer Authentication Tool (GUI or developer console)

For local development, use Epic's Developer Authentication Tool rather than bypassing Account Portal readiness:

1. Rotate the client secret exposed in chat. Never paste the replacement or commit it.
2. Start Epic's Developer Authentication Tool on **port 6547** on your own PC.
3. Sign in through the tool with an Epic account permitted to access your development application.
4. Save that credential in the tool with a name such as `PlayerOne`.
5. Keep the tool running. The project contacts `localhost:6547` on the SAME PC, not this sandbox.
6. In the window, enter `PlayerOne`, the rotated secret, then click **Log in with Epic**.
   The commands below are only for developers running the retained console diagnostic from a source build:

```powershell
# Only set this if you replaced the entire client, not just its secret:
# $env:EOS_CLIENT_ID = 'replacement-client-id'
& "$Project\Build\LoginSequence.ps1" -DeveloperCredential 'PlayerOne'
```

The runner defaults to `developer`, prompts for the rotated client secret without echoing it, and clears the
secret environment variable in `finally`. The credential name is NOT your password or client secret.
The native executable still calls the real EOS Auth and Connect APIs; the local tool is not a simulated identity.
The overall login timeout is 180 seconds. The environment secret is temporarily readable by the local process;
this is not protected server-side storage.

If Auth succeeds but Connect reports `account_creation_required`, confirm this is a genuinely new test player:

```powershell
& "$Project\Build\LoginSequence.ps1" -DeveloperCredential 'PlayerOne' -AllowCreateUser
```

This permits a real PUID creation in the Dev deployment. Do not use it to work around an identity-linking problem.
For a second test player, sign in another authorized account in the tool, save it as `PlayerTwo`, and run again
with that credential name. One successful development account does not prove public player access.

## Account Portal mode

The supplied SDK's `eos_auth_types.h` explicitly requires Windows Account Portal applications to be launched
through EOS Bootstrapper with the EOS redistributable installed. The earlier overlay-disabled configuration
was incorrect for this path and has been removed. The project now checks desktop crossplay readiness on Windows.

For this path, configure Epic's Bootstrapper to launch the executable with local environment
`EOS_LOGIN_METHOD=accountportal` and the rotated client credential. Follow the Bootstrapper instructions for
this exact SDK distribution. The runner's `-LoginMethod accountportal` is a diagnostic selection, NOT a replacement
for launching through the Bootstrapper. A direct launch can correctly refuse with `account_portal=not_ready`.
We have not fabricated Bootstrapper command-line arguments or bundled an untested installer.

## Login evidence to share

A successful run must exit zero and include all three lines below. This is the EXPECTED format, not captured proof:

```text
auth=success epic_account_id_valid=1
connect=success product_user_id_valid=1
LOGIN_VERIFIED auth=success connect=success
```

The Windows runner writes UTC timestamps, executable hash, exit code, and redacted diagnostics to
`Build/Output/Login-*.log`. Share that log and `BuildEvidence.json`, not secrets or browser codes.
A local log is diagnostic evidence, not a signed assertion a server should trust.

## Reproduce Linux checks

With the Linux runtime in `SDK/Bin` and matching headers in `SDK/Include`:

```bash
python3 Frontier/Projects/Project-Networking/Build/ToolchainSequence.py \
  --sdk-root /path/to/SDK --slate-root /path/to/Slate
python3 VisualProof/Networking/RunChecks.py --slate-root /path/to/Slate
```

The checker strips credential environment variables, exercises the real SDK, and never starts player login.
It also writes fresh evidence and fails nonzero on a regression. The Linux `.so` is for ABI verification;
the `.frontier` specification remains targeted at the Windows DLL.

## Open the project through Frontier

After building the Windows engine normally, use a securely configured local session with `EOS_CLIENT_SECRET`,
`EOS_LOGIN_METHOD=developer`, and `EOS_DEVELOPER_CREDENTIAL=PlayerOne`, with the developer tool running:

```powershell
$env:PATH = "$Project\Build\Output;$env:PATH"
& 'C:\Path\To\Frontier.exe' "$Project\ProjectNetworking.frontier"
```

Diagnostics go through Frontier's diagnostic reception. There is no login UI yet. Close and reopen the project
to rerun the one-shot sequence. Remove local secret environment variables when finished. Start with the console
route so unrelated renderer or empty-scene issues do not mask authentication failures.

## GitHub Actions Windows binary

`.github/workflows/networking-windows.yml` builds on Windows Server 2022 with MSVC x64, C++20 and `/MD`.
The runner fetches the latest Slate branch for its ABI header and records the resolved commit in build evidence.
`WindowsSdkManifest.json` pins every downloaded SDK input by size and SHA-256. Download access relies on the
existing user-shared Drive links; the workflow does not change their permissions or receive any client secret.
It runs real SDK initialization/shutdown, missing-secret refusal, and the login-order checks before packaging.

The current artifact contains the windowed EXE, project DLL, GUI library licenses, project specification, scene,
evidence, and instructions. Older console releases remain historical.
It is NOT a full Frontier engine build. SDK headers and import libraries are excluded from the downloadable package. At the user's request, the Windows
x64 EOS runtime is now included beside the EXE; no manual copy is required. No credentials are baked in.
Actions artifacts expire after 14 days; the workflow also publishes a prerelease with a persistent, signed-out
download link. Each release targets the exact source commit on this session branch.

### Windows build result

Actions run `37583886172` completed successfully for source commit
`1a430f3fc2c59ea1083380f6283623da119de1f8`. `VisualProof/Networking/WindowsActions.json` records the
reported job and step results. The package contains the runner-generated `WindowsChecks.log` and
`BuildEvidence.json`; these are distinct from the earlier Linux-only results above.

Download: https://github.com/c7egoist/Frontier/actions/runs/37583886172/artifacts/11466181637

The sandbox could not mirror the artifact from GitHub's Azure download host. The artifact itself was uploaded
successfully and is available through the GitHub link (sign-in may be required).

### GUI release result

Actions run `37587675861` passed for source commit `87186b4cd69433d351e661ce591de4ad4cf29e98`.
The native GLFW/ImGui window rendered on the Windows runner using CI-only Mesa software OpenGL. Its screenshot,
SDK startup results, and missing-input refusal evidence are included in the ZIP under `Build/Output`.
The extracted ZIP was also launched successfully. Player authentication was not attempted.
`VisualProof/Networking/WindowActions.json` records the successful job and steps.

Current GUI download:
https://github.com/c7egoist/Frontier/releases/download/networking-test-37587675861/Project-Networking-Windows-x64.zip

Extract and open `NetworkingLogin.exe` at the package root. Older console-only releases are superseded.

## SDK lifetime fix and Epic social features

The previous window called EOS_Shutdown during its startup SDK check, then tried EOS_Initialize during login.
That was incorrect: `eos_init.h` says initialization occurs once and no SDK calls are permitted after shutdown.
This caused the reported `EOS_AlreadyConfigured`. The replacement keeps one SDK lifetime through checks, retries,
login and social operations; only the final application exit shuts it down. The test suite now exercises this
sequence in ONE PROCESS, unlike the earlier separate-process startup checks. Terminal shutdown is guarded.

The secret field is an EOS application client credential, not an Epic user password. The value pasted in chat is
exposed and must be rotated locally in the portal. The app does not embed or reuse it. The secret field is cleared
after submission, so an empty-field warning on a retry means it needs to be entered again, not that EOS rejected it.
The new paste button and ImGui clipboard callback use Windows Unicode clipboard APIs, report non-text/busy
clipboards, and never echo copied text. Try the portal's copy icon, not copying the masked asterisks.

Enable **Request Friends + Presence permissions** before login. The app requests Basic Profile, Friends List and
Presence scopes; configure the matching permissions on the linked Epic Account Services application. Successful
Auth + Connect keeps the platform alive and ticking. The Friends panel queries the real EOS friends list and
sanitized display names. It renders at most 128 returned records, with an explicit returned/total count. Names and
account IDs are not included in exported logs. Refresh friends performs a new query after prior requests finish.
Only friends visible to this application's permissions/consent may be returned; this is not a promise of the entire
Epic launcher friends list. Display names may be unavailable. Friendship status is not online-presence status.

**Epic login overlay (Account Portal)** uses EOS Auth, not an ImGui imitation. **Open Epic overlay** calls
EOS_UI_ShowFriends; the Epic Social Overlay handles friend invitations. The default shortcut is Shift+F3.
The app enables the Windows OpenGL overlay capability and respects overlay-exclusive input notifications.
Overlay readiness errors now identify missing Bootstrapper launch, missing/stopped redistributable service,
overlay installation, or trust/load failures. No friends or overlay call is allowed before successful login.

The supplied EOS Flattened folder has Include/Lib/Bin but no Bootstrapper or redistributable installer. The SDK DLL
alone does NOT install these. Obtain the matching official tools from the original Epic SDK distribution, install
the redistributable and launch the app through a properly configured EOS Bootstrapper. These prerequisites have
not been bundled or silently downloaded. Developer Auth Tool is still a separate fallback for testing identity;
it does not by itself satisfy Social Overlay setup. Live login, friend retrieval and visible Epic overlay operation
cannot be verified in credential-free CI. Windows CI tests the UI, SDK lifetime and unauthenticated guards only.

Disconnect closes this local platform, not a global Epic-account sign-out. Long-lived token refresh is not yet
implemented; an expired session must sign in again. Never use this client's status as a premium entitlement check.

### Friends/lifetime-fix release result

Actions run `37590703228` passed for source commit `1ea2afbe168e4651b2fd13819674b2c8f4364ddd`.
It includes a same-process SDK initialization/retry/final-shutdown regression, unauthenticated social guards,
real Windows GUI rendering with CI-only Mesa, and launch checks from the extracted ZIP.
`VisualProof/Networking/SocialActions.json` records the successful job results.
Actual player login, friends retrieval and Epic overlay display are still not verified by CI.

Latest download:
https://github.com/c7egoist/Frontier/releases/download/networking-test-37590703228/Project-Networking-Windows-x64.zip
