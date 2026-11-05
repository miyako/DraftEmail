# Interactive host integration

Install the `DraftEmail` component in an interactive 4D desktop host. No plug-in,
SMTP credentials or automated Send operation is involved. These snippets are
host form/method code, not component initialization or unattended tests.
Namespace is `DraftEmail`; select the explicit native target on Windows.

## Compose button

Keep the returned shared job handle in the form. `compose` starts a unique live
worker and does not wait for the mail client on the UI process. No regular
Object/Collection is used for cross-process mutable storage.

```4d
// Compose button: On Clicked
var $target; $client : Text
var $tool : cs.DraftEmail.Composer
var $request : cs.DraftEmail.ComposeRequest
var $start : Object
$target:="macOS-universal"
$client:="apple-mail"
If (Is Windows)
    $target:="Windows-x64"
    $client:="simple-mapi"
End if
If (Form.composeJob=Null)
    $tool:=cs.DraftEmail.Composer.new($target)
    $request:=cs.DraftEmail.ComposeRequest.new($client)
    $request.to.push("recipient@example.invalid")
    $request.subject:="Review before sending"
    $request.body:="Plain text draft. Only you can choose Send."
    // Optional absolute existing file path, not a File object:
    // $request.attachments.push("/Users/example/Documents/report.pdf")
    $request.interactive:=True
    $start:=$tool.compose($request)
    If ($start.ok)
        Form.composeJob:=$start.job
        Form.composeTarget:=$target
        SET TIMER(30)
    Else
        ALERT($start.error.code+": "+$start.error.message)
    End if
End if
```

Initialize `Form.composeJob:=Null` on form load. Reject repeated clicks while
the handle is present (or disable the button). The helper requires an interactive
desktop with Apple Mail's AppKit email sharing service on macOS, or
**classic Outlook x64** with Simple MAPI on Windows. New Outlook is unsupported.
macOS cc/bcc are not exposed by the sharing service and nonempty lists are
rejected, not silently omitted. The Boolean opt-in is the caller's
responsibility; it is not proof of human presence.

## Form timer and deterministic completion callback

In the form method's **On Timer** branch:

```4d
var $tool : cs.DraftEmail.Composer
var $snapshot : Object
If (Form.composeJob#Null)
    $tool:=cs.DraftEmail.Composer.new(Form.composeTarget)
    $snapshot:=$tool.poll(Form.composeJob)
    If ($snapshot.ok)
        If ($snapshot.finished)
            Form.composeJob:=Null
            SET TIMER(0)
            EmailDraft_completed($snapshot.jobId; $snapshot.result)
        End if
    Else
        Form.composeJob:=Null
        SET TIMER(0)
        ALERT($snapshot.error.message)
    End if
End if
```

`EmailDraft_completed` is a host project method, called exactly once by the
form after clearing its handle. It runs on the form's own process; the component
does not guess callback method visibility across project boundaries:

```4d
#DECLARE($jobId : Text; $result : Object)
var $staging : Object
If ($result.ok)
    $staging:=$result.value.staging
    Case of
        : ($result.value.status="handed_off")
            ALERT("Draft handed to your mail client. Review and choose Send yourself.")
        : ($result.value.status="cancelled")
            ALERT("Mail composition was cancelled.")
    End case
Else
    $staging:=$result.staging
    ALERT($result.error.code+": "+$result.error.message)
End if
If ($staging#Null)
    If ($staging.retained)
        // Retain this token in host-owned persistent state before form closure.
        // Never auto-delete: the mail client may still need the attachment paths.
        Form.lastStaging:=$staging
    End if
End if
```

This callback pattern needs no host `CALL WORKER` wrapper, shared PID map, global
error handler or normal Object passed by reference. `CALL WORKER` and shared
storage are managed internally by the component. No timer should call native
`compose` repeatedly.

After the user has confirmed the mail client consumed/saved the attachments
and no draft needs the retained paths, use an explicit cleanup action:

```4d
var $tool : cs.DraftEmail.Composer
var $cleanup : Object
$tool:=cs.DraftEmail.Composer.new(Form.composeTarget)
$cleanup:=$tool.cleanup(Form.lastStaging.token)
If ($cleanup.ok)
    Form.lastStaging:=Null
Else
    ALERT($cleanup.error.code+": "+$cleanup.error.message)
End if
```

For multiple drafts, retain every job's token, not just `Form.lastStaging`.
The minimal example's form state is not durable storage; persist tokens through
your application's existing storage if cleanup must survive application exit.

## Closing, timeout, errors and limits

Stopping the timer/closing the form does **not** cancel a native composer.
Retain the job elsewhere if you need to observe its result after form closure;
otherwise release the form's reference. The worker publishes and self-terminates
after its bounded 600-second outer native execution, independent of polling.
Application shutdown is not durable job delivery. No cancellation/automatic
retry API is provided: killing a 4D worker only requests termination after its
current task and cannot safely close an external mail window.

Validation uses 30 seconds and is noninteractive, but still synchronous. Perform
it outside time-critical UI paths if necessary. Only plain text is supported;
do not supply HTML/RTF, inline-image/CID content, authentication credentials,
server sending or a `mailto:` URL. Attachments must be readable absolute files.
See the CLI's documented size/count limits; capability output is not account,
permission or network validation.

Never run interactive composition in CI, tool4d, 4D Server, scheduled/background
automation or a headless desktop. Use the noninteractive host fixture to test
validation/decoding instead, and manually test actual draft UI with consent.
`handed_off` is not sent/delivered; `cancelled`, timeout and an error are not
grounds for automatic retry because a draft may already exist.
