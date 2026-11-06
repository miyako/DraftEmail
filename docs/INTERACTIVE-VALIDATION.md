# Interactive desktop acceptance

These checks require a configured desktop mail client and explicit permission
to open drafts. They are **not executed by CI or unattended host tests**.
Do not send messages as part of these checks. Do not change the user's mail
defaults, accounts or profile configuration without separate authorization.

Record the Git commit, executable SHA256, OS/architecture, 4D version, client
version, provider architecture, account configuration category, exact request,
observed result and retained attachment paths. Use synthetic addresses and
non-sensitive documents. Never record account credentials.

## Required matrix

| Environment | Initial status | Required observation |
| --- | --- | --- |
| Apple Silicon macOS / Apple Mail | not_run | Main 4D form remains responsive; one editable plain-text draft with exact fields and attachments |
| Intel macOS / Apple Mail | not_run | Same checks using native Intel 4D and helper, not Rosetta evidence |
| Windows x64 / classic Outlook x64 | not_run | Unicode MAPI provider, exact To/Cc/Bcc/subject/body and every attachment |
| Windows x64 / mismatched provider | not_run | Explicit compatibility error; no unreported field/attachment loss |
| Windows x64 / missing or unconfigured provider | not_run | Explicit provider/configuration diagnostic, not success |
| Windows x64 / new Outlook only | not_run | Explicit unsupported/unavailable error, no `mailto:` or `.eml` fallback |

A successful native helper test does not replace a real 4D host check.
Source-host and unpacked compiled-component checks are separate observations.
The helper's native status and the mail client's visible behavior must both
be recorded; an apparent draft alone must not approve a malformed protocol.

## Procedure

1. Use the documented 4D button/worker integration with a synthetic request.
   While the client is opening or displaying a modal dialog, interact with
   the original 4D form. Verify it remains responsive and completion is
   delivered once to the documented callback.
2. Verify To and supported Cc/Bcc fields using multiple addresses. On Apple
   Mail, supply nonempty Cc/Bcc separately and verify explicit rejection before
   opening a draft. Do not accept a draft missing requested recipients.
3. Use a subject with Unicode, spaces, quotes and punctuation, a multiline
   plain-text body and at least three attachments, including a Unicode filename,
   spaces and duplicate basenames from different folders. Inspect every field
   and attachment, including file content. No HTML interpretation is expected.
4. After handoff, delete or move only the disposable original test files.
   Verify all attachments remain usable in the editable draft and a saved draft
   after restarting the mail client. Inspect retained copies and follow the
   documented cleanup procedure only after discarding all dependent drafts.
5. Close/discard the draft without sending. Record whether the backend actually
   observes cancellation. Apple Mail handoff cannot observe the person's later
   decision. Never reinterpret `handed_off` as sent or delivered.
6. Exercise a missing file, directory path, inaccessible file, NUL/type-invalid
   request, unsupported client, unknown field and unsupported HTML field.
   Each must produce an explicit error without opening a draft or dropping data.
7. Test a configured provider returning a cancellation/error, and a timeout
   using a disposable desktop session. A timeout is indeterminate: inspect the
   client before retrying to avoid duplicate drafts. Verify attachment copies
   needed by an already-open draft are not prematurely deleted.
8. Run two independent compose requests and verify isolated request state,
   distinct attachment staging and correctly associated completion results.
   Respect any backend serialization restriction reported by capabilities.

Headless rejection should be exercised in an isolated non-interactive session,
not by disrupting the user's desktop. Staging cleanup must never delete user
originals, unrelated temporary files or copies still required by another draft.

## Reporting

Use `passed`, `failed`, or `not_run` per row. Include observed limitations and
provider-specific behavior, not just a single green status. Keep native CLI,
source 4D, compiled-package 4D, interactive composition, dependency-manager
installation and signing evidence distinct. No current `not_run` row becomes
passed merely because the implementation or protocol exists.
