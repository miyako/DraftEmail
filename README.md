# DraftEmail

A **CLI-backed 4D desktop component** for preparing an editable, plain-text
email with local invoice, quote or delivery-note attachments. The person reviews
the draft and chooses **Send** in their mail client. This component never sends
mail automatically, and a successful handoff is not evidence of delivery.

Repository/component/package: `DraftEmail`; 4D namespace: `DraftEmail`; public
feature class: `Composer`; bundled executable: `draft-email-cli`; implementation:
C++17 with Objective-C++/AppKit on macOS and Simple MAPI on Windows.
This is not a native 4D plug-in and installs no plug-in in the host.

## Supported clients and deployment

| Platform | Backend | Scope |
| --- | --- | --- |
| macOS 11+, universal Intel/Apple Silicon | AppKit `NSSharingServiceNameComposeEmail` | Apple Mail; To, subject, plain-text body and attachments. Cc/Bcc are explicitly unsupported by this public API. |
| Windows x64 | Unicode Simple MAPI, `MAPISendMailW` | Compatible x64 desktop providers; classic Outlook is the initial interactive validation target, not a validated configuration yet. |
| New Outlook, Windows ARM64, server/headless sessions | None | Unsupported. No SMTP, `.eml`, `mailto:`, browser automation or lossy ANSI fallback. |

Use the component in an interactive **4D client**, not 4D Server or a web
request. Mail must be installed and configured on that client. Generated files
must already exist on the client machine, not merely on the server. Windows
requires compatible process/provider bitness and a Unicode-capable provider.
An installed provider is not proof of working account configuration.
No arbitrary default-mail-client compatibility is promised.

The helper validates every field and readable regular-file attachment before
handoff. Unsupported fields are errors, never silently dropped. The helper
uses private attachment copies; user originals are never deleted. Keep retained
copies until the draft has been sent, saved or discarded and the client no
longer needs them. Consult the protocol and returned staging information before
cleanup; a timeout is not proof that the client did not receive a draft.

## 4D API

See [Composer](component/DraftEmail/Documentation/Classes/Composer.md) for the
typed API, worker lifecycle, results and button integration.
`cs.DraftEmail.Composer` is the host-facing class; component-internal code uses
`cs.Composer`.

Choose `"macOS-universal"` on macOS and explicitly `"Windows-x64"` on Windows.
Use `capabilities()` before presenting optional fields. `validate()` performs
non-interactive validation; it does not open Mail or establish provider readiness.
Composition runs in a 4D worker so an open mail dialog cannot freeze the main
form. Follow the documented dispatch/completion example rather than waiting
synchronously in a button handler.

Every native result uses protocol version 1. Errors preserve native diagnostics
and process stdout, stderr and status when available. Prelaunch failures do not
invent an exit status. Cancellation is reported only when the backend actually
observes it. Apple Mail handoff cannot observe the person's later Send/Cancel
decision.

## CLI protocol

The executable accepts command-line arguments directly, without a shell.
Requests and responses are strict UTF-8 JSON. Requests carry `protocol:1`, an
explicit supported `client`, `to`, `cc`, `bcc`, `subject`, `body`, and absolute
local `attachments` paths. Composition additionally requires explicit
`interactive:true`. Recipient collections contain mailbox addresses, not
unparsed header lines. HTML is not supported.

| Invocation | Contract |
| --- | --- |
| `draft-email-cli --version` | 0 / `{"protocol":1,"version":"0.1.3"}` + LF / empty stderr |
| `draft-email-cli capabilities` | Non-interactive backend and field capability information |
| `draft-email-cli validate --request JSON` | Non-interactive validation; `{"protocol":1,"status":"validated"}` + LF on success |
| `draft-email-cli compose --request JSON` | Explicitly interactive; handoff, observable cancellation, or error; never automatic sending |

The full limits, errors, staging lifetime, capability schema and platform
semantics are specified in [the protocol](docs/PROTOCOL.md).
Do not put sensitive message bodies or recipient addresses into logs. Command-line
requests may be visible to local process-inspection tools; this is a local
desktop trust boundary, not a secure channel between users.

## Build and non-interactive checks

Read [AGENTS.md](AGENTS.md) for the template's provenance and acceptance gates.
Only C++ is selected; do not restore other language starters.
Python 3.10+, CMake and native Xcode/Clang or x64 MSVC are required.
The CMake build uses system AppKit/Foundation or Windows APIs and the selected
Windows CRT policy; inspect [redistribution notices](component/DraftEmail/Resources/licenses).

```sh
python3 scripts/doctor.py --root .
TEST_CLI_LANGUAGE=none python3 -m unittest discover -s tests -v
python3 -m unittest discover -s tests -p test_cli.py -v
python3 scripts/generate_icons.py --check
python3 scripts/build_cli.py --root . --target macos-arm64
```

Select the actual native target: `macos-arm64`, `macos-x64`, or `windows-x64`.
C++ cross mode is deliberately unsupported. CI runs all three native builds
with their non-interactive vectors, uploads binaries and provenance manifests,
and assembles a real universal macOS executable plus Windows x64 executable.
All inputs and manifests must come from the same commit; do not mix artifacts.
No unattended test opens a compose window, changes accounts/defaults, or sends
a message.

### Real 4D compilation and package acceptance

The free pinned tool4d download is checksum-verified; no activation/signing
secret is required for unsigned local checks.

```sh
python3 scripts/ci_config.py download-tool4d --root .
python3 scripts/ci_config.py assemble --root .
python3 scripts/test_4d.py --root . \
  --tool4d .tools/tool4d.app/Contents/MacOS/tool4d
python3 scripts/compile_component.py --root . \
  --tool4d .tools/tool4d.app/Contents/MacOS/tool4d
python3 scripts/package_component.py --root . \
  --repository miyako/DraftEmail --tag v0.1.0 --unpack-test
python3 scripts/test_4d.py --root . \
  --tool4d .tools/tool4d.app/Contents/MacOS/tool4d \
  --component build/release-test/DraftEmail.4dbase
```

Assembly and packaging require **all three current native artifacts and
manifests**. A local macOS build alone cannot produce an accepted cross-platform
ZIP. The source and final compiled host fixtures exercise capabilities,
validation and generic process errors without opening drafts.
Compiler success is not compiled-package loading success.

`scripts/accept_local.py --repository miyako/DraftEmail --tool4d PATH` runs the
template pipeline when all required artifacts are available. The final package
is `dist/DraftEmail.zip`, with adjacent `DraftEmail.acceptance.json` binding
its hash, revision and executed checks. `build/acceptance.json` describes the
latest attempt, including failures; an old ZIP is not proof of that attempt.
`passed`, `failed` and `not_run` are distinct. Partial local acceptance is
**not release ready**.

## CI, package installation and publication

Exactly two workflows are retained: **CI** and manual main-only **Publish**.
CI performs ordinary non-interactive validation without publishing. With the
repository Actions variable `FOURD_ENABLED=true`, it additionally downloads
the pinned free tool4d, compiles, and tests the unpacked compiled ZIP, retaining
`compiled-component` and evidence for 30 days. Missing native artifacts or
failed host checks stop acceptance. Merely green CLI jobs do not validate
interactive Apple Mail, classic Outlook, Windows providers or native 4D on
every platform.

For an explicitly authorized **signed test package without publication**, run
**CI** on `miyako-draft-email-component` with `signed_test=true`. This path
is restricted to that trusted branch in `miyako/DraftEmail`, uses a read-only
repository token, and exposes the seven signing secrets only to its signing
step. It rebuilds all native targets from the dispatch SHA, runs source and
unsigned compiled-host checks, signs the macOS CLI and component libraries,
notarizes/staples a DMG, and tests the final ZIP rebuilt from that DMG.
Download `signed-test-component` (ZIP, DMG, signing proof and acceptance JSON)
from the run's artifacts within 30 days. The current version is signing metadata
only: this path does not bump versions, create a tag or GitHub Release, or
invoke Publish. Pushes, pull requests and reusable release calls cannot activate
this secret-bearing job. Interactive email testing remains separate.

Publication requires separate authorization. Publish accepts patch/minor/major,
commits synchronized metadata to main, builds that exact revision and only
creates a tag/release after the required final-package gates. Publish signing
is controlled only by `release-policy.json`: an exact version entry requires
signing/notarization; unlisted versions are unsigned. There is no Publish
dispatch override. The planned first public release, `0.1.2`, requires Apple
signing/notarization under this policy. The separate signed-test CI path is unchanged. See
AGENTS.md for the signing lifecycle. Do not copy
credentials from another repository. No release or tag is required for
development.

DMG stapling and staple validation each retry at most twice, after 5 and 15
seconds, only for exit 68 with an explicit transient `NSURLErrorDomain`
network error. Retry diagnostics are credential-redacted. Other failures and
exhausted retries stop signing; successful validation and final-ZIP host
acceptance remain mandatory.

Once an accepted asset exists, manually install its unpacked `DraftEmail.4dbase`
in the host's `Components/` folder. Signed test artifacts are not published
Dependency manager releases. Dependency-manager installation must be tested
separately using an actual published asset. Repository visibility is managed
separately by the owner; changing it is not part of building a package.

## Validation boundaries

Use the [interactive acceptance procedure](docs/INTERACTIVE-VALIDATION.md) only
with explicit desktop-test permission. Interactive Apple Mail composition,
actual classic Outlook, provider bitness,
missing/misconfigured accounts, Unicode recipient fidelity, attachment persistence
through editing/saving, user cancellation, and GUI timeouts require deliberate
tests on real configured desktop clients. They are **unvalidated unless recorded
as actually exercised**. No permission to open unattended drafts or send test
messages is implied by running this repository's tests.

The component's namespace uniqueness is an integration responsibility. Bundled
CLIs run as separate processes; no host-installed plug-in or runtime download
fallback is used. Retain the MIT [LICENSE](LICENSE) and all applicable runtime
notices when redistributing.
