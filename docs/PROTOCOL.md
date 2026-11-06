# DraftEmail CLI protocol 1

`draft-email-cli` is a standalone C++17 / Objective-C++ executable, not a 4D
plug-in. It never invokes a shell, sends mail automatically, configures accounts,
or changes the default mail client. A user can choose to send in the native UI.
Success is **handoff**, never proof of delivery or persistence of a saved draft.

## Commands and framing

Arguments are literal UTF-8 strings (Windows uses a strict UTF-16 → UTF-8
conversion from `wmain`). Exactly one JSON object plus LF is emitted on one
stream; no banners or logs. All objects contain numeric `protocol:1`.

| Arguments | Result |
| --- | --- |
| `--version` | stdout exactly `{"protocol":1,"version":"0.1.3"}\n` |
| `capabilities` | Noninteractive static platform contract below; does not launch a client, enumerate accounts, or prove provider availability |
| `validate --request <JSON>` | Fully checks schema, supported fields, paths and readable regular files without staging or UI; stdout exactly `{"protocol":1,"status":"validated"}\n` |
| `compose --request <JSON>` | Requires explicit `interactive:true`; checks availability, stages attachments, invokes the native compose UI |
| `cleanup --staging <token>` | Explicitly removes this user's retained staging batch only; stdout exactly `{"protocol":1,"status":"cleaned"}\n` |

Successful commands exit 0 with empty stderr. Errors have empty stdout and
stderr `{"protocol":1,"error":{"code":"CODE","message":"DETAIL"}}\n`.
If staging completed before a compose error, the same object additionally
contains `staging` as below. Messages are diagnostics, not a stable enumeration.
No native error, missing callback or timeout is relabeled cancellation.

## Request

```json
{"protocol":1,"client":"apple-mail","to":["person@example.com"],"cc":[],"bcc":[],"subject":"Review","body":"Plain text only","attachments":[],"interactive":true}
```

Every listed field except `interactive` is required. No unknown fields,
duplicate JSON keys (including escaped equivalents), coercion, implicit
defaults, NUL characters, malformed UTF-8, lone UTF-16 surrogate escapes,
comments, trailing commas or trailing content are accepted. `protocol` must be
the JSON integer token `1`, not Boolean, `1.0` or `1e0`. `interactive` if present
must be Boolean and must be true for compose. Validation accepts either client
on either platform; composition supports only the native platform's client.

* `client`: exactly `apple-mail` or `simple-mapi`.
* `to`, `cc`, `bcc`: arrays of bare ASCII mailbox addresses, no display names,
  quoted local parts, comments, address literals or SMTPUTF8. ASCII dot-atom
  local parts and DNS labels (including caller-supplied IDNA A-labels) are
  accepted; invalid label edges, consecutive dots and CR/LF are rejected.
  Maximum 254 bytes/address, 64-byte local part and 63-byte DNS labels.
  This syntactic check does not resolve domains or assert deliverability.
  Empty arrays and an empty overall recipient set are allowed for user editing.
* `subject`, `body`: Text, including empty Text. Subject cannot contain CR/LF.
  Body is plain text, never interpreted as HTML by this helper.
* `attachments`: arrays of absolute native file paths. Files must be readable
  regular files; directories, devices, FIFOs, symlinks (including ancestors),
  reparse points, `.`/`..` segments and missing files are rejected. Windows
  accepts drive-absolute paths only, not UNC/device paths or alternate streams.

Limits are UTF-8 byte counts, not user-perceived character counts: request
24,576 bytes; subject 998; body 16,384; path 4,096; JSON nesting 16.
At most 100 total recipients, 20 attachment entries, 25 MiB/file and
100 MiB/batch (duplicate entries count separately).
These conservative limits also keep requests below typical Windows command-line
limits; they do not increase the command-line limits imposed by the caller/OS.
No field is silently truncated, discarded, converted to ANSI or mapped into a
different recipient category.

## macOS: Apple Mail

Exact capabilities output:

```json
{"protocol":1,"backend":"ns-sharing-service","client":"apple-mail","fields":{"to":true,"cc":false,"bcc":false,"subject":true,"body":true,"attachments":true},"interactiveRequired":true,"timeoutSeconds":540,"deliveryConfirmed":false,"stagingCleanup":true}
```

Public `NSSharingService.h` exposes writable `recipients` and `subject`;
`messageBody` and `attachmentFileURLs` are read-only. There is **no public
cc/bcc configuration API**. Nonempty cc or bcc therefore returns
`unsupported_field`, even during validate. No KVC, private selectors,
AppleScript, accessibility automation or `mailto:` fallback is used.

`NSSharingServiceNameComposeEmail` gets recipients/subject and shares a
plain `NSString` plus staged file `NSURL` items. The main thread initializes
AppKit, retains the service, item array and delegate and pumps the AppKit event
run loop until a delegate callback or a monotonic bounded wait expires.
The helper requires this user's active console login and an installed
`com.apple.mail`; background/loginwindow sessions fail preflight.
Service absence or `canPerformWithItems:NO` is an explicit backend error.
Availability does not prove an account is configured.

`didShareItems` means `handed_off`. Only Cocoa `NSUserCancelledError` means
`cancelled`; other callback failures remain errors. Neither success nor
cancellation proves that an attachment can safely be deleted.

## Windows: compatible Unicode Simple MAPI providers

Exact capabilities output:

```json
{"protocol":1,"backend":"simple-mapi-w","client":"simple-mapi","fields":{"to":true,"cc":true,"bcc":true,"subject":true,"body":true,"attachments":true},"interactiveRequired":true,"timeoutSeconds":540,"deliveryConfirmed":false,"stagingCleanup":true,"providerPolicy":"registered-unicode-x64","newOutlook":false}
```

Support is conditional: native Windows x64, a native x64 process, and a
configured default Unicode Simple MAPI provider. Classic Outlook is the initial
Windows interactive validation target, not a provider-name allowlist.
The current-user default takes precedence over the machine default; selected
client registrations are read in the 64-bit view of `Software\Clients\Mail`.
A present current-user client key takes precedence over the machine key;
incomplete overriding registration fails closed rather than silently switching.
`DLLPathEx` takes precedence over `DLLPath`; `REG_EXPAND_SZ` paths are expanded.
The selected DLL must be a readable drive-absolute x64 PE DLL with a direct
named `MAPISendMailW` export. Its header/export table is inspected as data:
provider code is not loaded or executed during preflight. Forwarded exports,
unreadable/non-x64 DLLs and missing Unicode exports are unsupported because
the helper cannot establish their Unicode/bitness compatibility.
Classic Outlook's registered DLL must specifically be `OLMAPI32.DLL`.
Missing default/provider/path registration returns `missing_configuration`;
failed compatibility checks return `unsupported_provider`.

New Outlook is explicitly unsupported, as are 32-bit providers, Windows
ARM64/emulation, services/session 0 and invisible window stations.
Known new-Outlook client registrations are refused explicitly; other
registrations still must pass every DLL/Unicode check. This policy does not
claim every default client is compatible or that a checked provider is properly
configured. The helper also requires an accessible input desktop.
No registry, accounts, defaults or provider files are modified.

Load only the system `MAPI32.DLL` with `LOAD_LIBRARY_SEARCH_SYSTEM32`; resolve
`MAPISendMailW` dynamically. Recipient classes retain To/Cc/Bcc; all payload
and path strings remain Unicode. Always pass `MAPI_DIALOG | MAPI_LOGON_UI |
MAPI_FORCE_UNICODE`, never call without interactive dialog flags, and never
use `MAPISendMail`/ANSI or a Unicode helper that falls back to ANSI.
The force-Unicode flag is necessary because the Unicode stub may otherwise
convert for an ANSI-only provider. Such a provider explicitly fails.

Simple MAPI is a user-mediated send dialog, not a provider-independent
“save draft only” API. A user can send or cancel within it. `SUCCESS_SUCCESS`
maps only to `handed_off`; `MAPI_E_USER_ABORT` is observable `cancelled`;
other native statuses remain errors. No delivery claim is made.

## Deadline, concurrency and worker integration

Compose's native callback/provider deadline is **540 seconds**, not 30.
The 4D caller must use a worker and a process timeout greater than that deadline
(recommended 600 seconds) so the structured native timeout can be read.
Do not invoke compose on the host's UI thread or assume the stock 30-second
adapter is sufficient. Validate/capabilities/cleanup need no interactive UI.

On macOS, expiry detaches the delegate and exits without claiming the UI
was cancelled. The deadline uses a steady clock, unaffected by wall-clock changes.
On Windows the modal provider call runs on a private thread. The main thread
waits 540 seconds; expiry reports `timeout` and terminates the helper process
on normal process exit, without waiting forever for the provider. The provider
UI may disappear or remain in its own application; its final outcome is unknown.
This is not a request to cancel or send anything.

Requests have no shared account or draft state. Each compose has an independent
cryptographically random staging batch; simultaneous processes may produce
independent native dialogs. Provider concurrency/UI behavior is outside this
helper's guarantees. There is no idempotency/retry promise: **do not automatically
retry a timeout**, since a compose window or user action may already exist.

## Retained attachment staging and cleanup

Before native handoff, copy each validated source through an open file handle
into an exclusive private batch. Originals are never renamed, changed or
removed. Batches live under `~/Library/Application Support/DraftEmail-staging`
on macOS (owner-only directories/files), or
`%LOCALAPPDATA%\DraftEmail-staging` on Windows (protected owner/system ACL).
Files have an index prefix to disambiguate equal basenames. File contents may
be sensitive; there is no automatic expiration or background deletion.
Concurrent changes to source files may cause copying to fail; callers should
provide stable files. No live originals are handed to the native provider.

Compose success, cancellation, timeout and post-staging errors include:

```json
{"protocol":1,"status":"handed_off","staging":{"token":"0123456789abcdef0123456789abcdef","paths":["/absolute/private/copy"],"retained":true}}
```

With no attachments, staging is `{"token":"","paths":[],"retained":false}`.
The sample token/path are illustrative, not fixed output. Copies remain after
every native outcome: neither API proves deferred attachment consumption has
finished. Only explicitly run `cleanup --staging TOKEN` when the user confirms
the compose UI is closed and attachments are no longer needed, or the client
has independently saved/copied them. Cleanup cannot prove that condition.
It accepts exactly a 32-character lowercase hex token, never a path; it checks
the private root and refuses symlink/reparse or non-regular batch entries.
An absent/previously cleaned token is an error, not success.
Never pass an original path or delete the entire staging root as cleanup.
An interrupted helper may leave a batch without returning its token; an
administrator/user may inspect this dedicated directory after closing clients.
Pre-handoff staging failures remove the helper's incomplete new batch.

## Errors

| Exit | Codes |
| --- | --- |
| 2 | `invalid_arguments`, `invalid_utf8`, `invalid_json`, `invalid_request`, `unknown_field`, `unsupported_field`, `limit_exceeded`, `invalid_attachment`, `invalid_staging` |
| 3 | `unsupported_client`, `unsupported_architecture`, `interactive_session_required`, `client_unavailable`, `backend_unavailable`, `unsupported_provider`, `missing_configuration`, `unicode_mapi_unavailable`, `backend_error`, `staging_failed`, `internal_error` |
| 4 | `timeout` — outcome unknown, copies retained |

Filesystem exceptions not otherwise classified return `internal_error`.
Capabilities is a support contract, not an availability or compatibility
certification. Validate makes no account, GUI, provider, send or delivery claim.

## Dependencies, references and unattended verification

The JSON parser is original bounded C++17 code, with strict UTF-8, duplicate-key
and surrogate handling. No downloaded/vendored JSON dependency or network build
step is used. macOS links system AppKit/Security/SystemConfiguration and libc++;
Windows dynamically loads the OS MAPI shim and uses the selected toolchain's
static CRT. See bundled runtime/platform notices; no Mail, Outlook, tool4d,
Windows SDK or third-party plug-in is redistributed.

References inspected (2026-11-06):

* Installed macOS SDK `AppKit.framework/Headers/NSSharingService.h`.
* [Apple recipients](https://developer.apple.com/documentation/appkit/nssharingservice/recipients)
  and [sharing service](https://developer.apple.com/documentation/appkit/nssharingservice).
* [Microsoft MAPISendMailW](https://learn.microsoft.com/en-us/windows/win32/api/mapi/nc-mapi-mapisendmailw),
  specifically Unicode forcing, modal dialogs, cancellation and recipient rules.
* Prior art [miyako/4d-plugin-draft-email README](https://github.com/miyako/4d-plugin-draft-email)
  and [4DPlugin-Mail.cpp](https://github.com/miyako/4d-plugin-draft-email/blob/main/4DPlugin-Mail.cpp):
  public sharing-service recipient/subject/text/file usage only; reference,
  not copied source or runtime/build dependency. Its 4D SDK/threading layer is
  deliberately not used in this standalone component CLI.

Run `TEST_CLI_LANGUAGE=cpp python3 -m unittest discover -s tests -p test_cli.py -v`.
It builds the native executable and runs CTest private-copy/cleanup and pure
provider-selection/PE-export checks (injected registrations, no real registry
mutation or provider execution), plus
strict protocol/schema/Unicode/address/attachment/preflight tests. CTest uses
a separately compiled staging implementation confined to its build directory,
not the user's real staging directory. No test invokes a valid compose,
opens drafts, sends mail, creates accounts or changes defaults.
Then run the template's native-only
`python3 scripts/build_cli.py --root . --target macos-arm64` (or actual native
supported target); it executes the platform-independent smoke vectors.
Windows compilation/runtime and all native UI/client acceptance remain
separate checks, never implied by Apple Silicon noninteractive tests.
