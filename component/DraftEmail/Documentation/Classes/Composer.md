# Composer

`cs.DraftEmail.Composer.new(target : Text)` wraps the bundled `draft-email-cli`
as a separate process. No 4D plug-in is used or installed. Use
`"macOS-universal"` (or omit the target on macOS) and explicitly
`"Windows-x64"` on Windows; native Windows ARM64 is unsupported.

## Public methods

| Method | Result |
| --- | --- |
| `version() : Object` | Synchronous protocol/version check; `value:{protocol:1,version:"0.1.3"}` |
| `capabilities() : Object` | Synchronous backend/platform capabilities, not a permission grant or delivery check |
| `validate(request : Object) : Object` | Synchronous, noninteractive validation; `value:{protocol:1,status:"validated"}` |
| `compose(request : Object) : Object` | **Nonblocking dispatch**, returning `{ok:true,jobId:Text,job:Object}`; no synchronous native composition on the calling/UI process |
| `poll(job : Object) : Object` | Nonblocking snapshot `{ok:true,jobId,state,finished}`; when finished, `result` is the final execution envelope |
| `cleanup(token : Text) : Object` | Explicit deletion of helper-owned staged attachment copies; `value:{protocol:1,status:"cleaned"}` |

`version`, `capabilities` and `validate` use a finite 30-second process timeout.
Do not call them repeatedly in a UI timer or assume validation makes a later
handoff succeed. Validation never opens a mail composer or sends mail. Cleanup also uses 30 seconds.

Capabilities have numeric `protocol:1`, Text `backend`/`client`, Boolean
`interactiveRequired:true`, numeric `timeoutSeconds:540`,
`deliveryConfirmed:false`, `stagingCleanup:true` and a `fields` Object of Boolean
`to`, `cc`, `bcc`, `subject`, `body`, `attachments`.
macOS reports `backend:"ns-sharing-service",client:"apple-mail"` and **cc/bcc
unsupported**. Windows reports `backend:"simple-mapi-w",client:"simple-mapi"`,
all six fields supported, a nonempty Text `providerPolicy` describing native
provider requirements, and `newOutlook:false`. These are supported contract
capabilities, not live consent,
provider or account discovery.

### Request

Use [`ComposeRequest`](ComposeRequest.md), or provide an Object with:

```json
{
  "protocol": 1,
  "client": "apple-mail",
  "to": ["recipient@example.invalid"],
  "cc": [],
  "bcc": [],
  "subject": "Review this draft",
  "body": "Plain text only.",
  "attachments": [],
  "interactive": true
}
```

`client` must be `"apple-mail"` or `"simple-mapi"`; the backend must match the
native platform. Recipient fields are Collections containing only Text.
Subject/body are Text, not HTML/RTF. Attachments are absolute native file-path
Texts (not File objects, folders, URLs, relative paths or file contents); the
native backend performs file/access/limit validation. Both the API and CLI
reject invalid types and NUL. The CLI remains authoritative for address syntax,
limits and backend availability; `validate` does not bypass those checks.
Do not put passwords, OAuth tokens or SMTP settings in requests. Native limits:
24,576 UTF-8 bytes per JSON request, 998 bytes subject (no CR/LF), 16,384 bytes
body, 100 recipients total, 20 attachments, 4,096 bytes per attachment path,
25 MiB per file, 100 MiB total attachment size. Mailbox addresses use the helper's strict ASCII
addr-spec subset, not display-name/HTML/Unicode mailbox syntax.

`compose` requires the Boolean `interactive:true`, explicitly set after a real
user action. The component cannot prove a human clicked a button. It is not a
background mail-sending service. Never invoke interactive composition from
tool4d, a scheduled task, a server/headless session or unattended CI.
An interactive desktop and configured supported mail client are required;
the native helper refuses unsupported/headless execution. Apple Mail uses
AppKit's `NSSharingServiceNameComposeEmail` sharing service, not AppleScript or
an automatic Send API. This service does not expose cc/bcc: **nonempty cc/bcc
are explicitly rejected**, never silently dropped. Simple MAPI uses wide-character
`MAPISendMailW` with dialog and force-Unicode flags. It requires a registered,
compatible **x64 Unicode Simple MAPI provider**; new Outlook is unsupported.
Classic Outlook is the initial manual validation target, not an exclusive
provider allowlist or a completed compatibility claim. Registration/PE checks
alone do not establish that a provider implements the required Unicode behavior.
Not all default mail applications implement Simple MAPI. A capability report is not proof of account
configuration, consent, client behavior or successful handoff.

### User Send only; handoff is not delivery

The backend opens or hands off a **plain-text draft** with recipients, body and
attachments. It never invokes a send command. Only the user in the mail client
can choose Send. `status:"handed_off"` means the backend accepted the handoff,
not sent, delivered, received, or persisted forever. `status:"cancelled"` is
a supported terminal outcome when the native client reports cancellation;
it is not a delivery failure. Apple Mail may report handoff before the user
subsequently sends, saves or closes its window. Do not retry automatically:
that can open duplicate drafts.

## Worker lifetime and polling

`compose` checks request types locally, serializes an immutable request snapshot,
creates a unique UUID-named `CALL WORKER` worker and returns immediately.
There is **one worker and one `New shared object` per call**. The formula receives
target/request as Text values and the shared job by reference; it does not
assume ordinary Objects or Collections are shared.

The returned `job` contains only scalar `jobId`, `state` and `resultJSON`.
The worker publishes its JSON result and terminal state together inside
`Use...End use`; `poll` reads a consistent snapshot with the same lock.
Ordinary decoded result objects never enter shared storage. Retain the job handle
in the requesting form/application state; poll it with a form timer, and remove
that reference after consuming the terminal result. No component-global job/PID
registry accumulates completed calls. Do not modify the job yourself.

Native composition runs on that worker with a **600-second outer process cap**,
not on the main UI process. Both backends' advertised callback/provider deadline is
540 seconds; the outer cap allows headroom for staging and native diagnostics.
Windows provider calls run on a private native thread and time out without claiming
the user's action was cancelled. The worker stays alive through `SystemWorker.wait()`,
publishes success/failure, and calls `KILL WORKER` to discard queued work and
terminate. Unexpected 4D worker errors are published as `worker_error` with raw
diagnostic details by a worker-local error handler before aborting the task.
Workers are independent: overlapping calls do not overwrite each other's state.
Apply application-level UI throttling (for example disable the Compose button
until the job finishes) to avoid opening many drafts.

There is deliberately **no cancellation API**. `KILL WORKER` only queues worker
termination after its current task and cannot safely cancel a native client's
in-progress UI. Closing a form or discarding a job handle does not cancel the
helper. Timeout/error does not retract a draft or prove that handoff never
occurred. The job survives form closure if another host reference retains it;
application exit can end workers without a terminal poll result. No process
can promise delivery or durable completion across application/OS termination.

## Attachment lifetime and explicit cleanup

The helper validates and privately copies attachments before handing them to the
mail client. A native compose response includes
`staging:{token:Text,paths:Collection,retained:Boolean}` in `result.value`;
native failures after staging can include the same object as `result.staging`.
With no attachments it has empty token/paths and `retained:false`. Otherwise
the token is 32 lowercase hexadecimal characters and `retained:true`.
Preserve this metadata even when composition is cancelled or fails.

Copies are intentionally **not removed automatically after handoff**, because
the client can still need their paths. Keep the token until the user confirms
the client has consumed/saved the attachments and no draft uses these paths.
Then explicitly call `cleanup(token)`; the helper deletes only its owned staging
directory, not original attachments or arbitrary caller paths. Never clean up
from the completion callback merely because handoff succeeded or a timer fired.
Closing a form, cancellation or timeout alone does not establish safe cleanup.
The host owns storage/lifetime of retained tokens and must account for unclaimed
copies after application shutdown. Cleanup of missing/invalid tokens fails
explicitly and is not silently treated as success.
If the helper is forcibly terminated before emitting staging metadata, retained
copies may be unreported; do not infer a token or delete arbitrary paths.

## Results and errors

Final success is:
`{ok:true,stdout:Text,stderr:Text,exitCode:0,value:Object}`.
For composition, `value` has numeric `protocol:1` and Text
`status:"handed_off"` or `"cancelled"` only. `"sent"`/`"delivered"` and malformed
or wrong-version responses are rejected. Validation requires `"validated"`.
Nonempty stderr is not itself failure.

Failures are `{ok:false,error:{code:Text,message:Text,...}}`. Native structured
error fields (including optional details) are retained, alongside available
stdout/stderr/exitCode. Prelaunch/type errors do not invent exit status.
Missing resources, unsupported target/platform, invalid arguments, launch errors,
system errors, timeout/abnormal exit, nonzero native status and invalid protocol
are distinct failures. A nonzero native result with missing/malformed protocol
diagnostics is `invalid_protocol`, never silent success. No shell, PATH fallback,
runtime download or permission repair is used.

See [`examples/README.md`](../../../../examples/README.md) for a button/timer
and callback pattern. `_decode`, `_composeWorker` and `_cli` are implementation
details by convention, not a 4D access-control boundary. Never call
`_composeWorker` directly from a form/main process.

## Runtime acceptance

The unattended host fixture invokes only version/capabilities/validation and
local refusals plus synthetic decoder cases. Two real `CALL WORKER` jobs exercise
shared-state polling and self-termination using deliberately unsupported targets,
which guarantee prelaunch failure without executing a native compose command.
It never opens a real composer.
The generic execution section separately launches the runner-supplied Python
interpreter as a test-only argument probe to verify exact File/Folder path
roundtrips, including the fixture's Unicode/spaces and POSIX quote characters.
It preserves the renamed native executable/version check. See [_Process](_Process.md);
the probe is not part of the production email CLI or a runtime dependency.
Interactive Apple Mail consent/draft behavior and Windows Simple MAPI behavior
need separate, consented manual native tests. Python tests alone do not establish
4D worker lifetime, quoting, compiled mode or cross-platform UI behavior.

Official APIs used:
[CALL WORKER](https://developer.4d.com/docs/commands/call-worker),
[New shared object](https://developer.4d.com/docs/commands/new-shared-object),
[Use](https://developer.4d.com/docs/keywords/use),
[KILL WORKER](https://developer.4d.com/docs/commands/kill-worker),
[ABORT](https://developer.4d.com/docs/commands/abort).
