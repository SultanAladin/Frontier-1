# Platform refusal investigation

The user screenshots show successful SDK initialization and credential storage,
followed by `platform=refused`, before Auth or Connect. They do not expose the
secret's actual length or establish the exact cause on that machine.

A local Linux probe used the real EOS 1.19.2.1 SDK and the existing platform IDs.
No real secret was used and no Auth/Connect API was called.

- A synthetic 41-character ASCII test credential created a platform successfully.
- Replacing it with 80 ASCII A characters returned a null platform handle.
- EOS's callback reported: `ClientCredentials.ClientSecret must be an ANSI string
  between 1 and 64 in length`, followed by `Invalid input platform options.
  EOS_EResult: EOS_NotConfigured`.

This reproduces one cause of the same generic failure, not a verified diagnosis
of the user's saved credential. The new preflight rejects invalid input before
saving/submission and explains how to replace the entire field. Remaining SDK
creation failures expose fixed redacted categories, never raw SDK messages.

Windows Actions 37596249555 passed (source c7911f8), including real platform
creation in the console and extracted GUI/OpenGL process, input-boundary tests,
and diagnostic redaction checks. See PlatformActions.json. The rebuilt Linux
backend passed the extended checks; see SdkChecks.log and SdkBuildEvidence.json.

No live user login, service startup, consent, or social overlay success was tested.
