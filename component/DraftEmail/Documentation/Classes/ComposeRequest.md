# ComposeRequest

`cs.DraftEmail.ComposeRequest.new(client : Text)` creates a typed request builder.
Choose `"apple-mail"` on macOS or `"simple-mapi"` on Windows.

| Property | Type | Initial value |
| --- | --- | --- |
| `protocol` | Integer | `1` |
| `client` | Text | Constructor argument |
| `to`, `cc`, `bcc` | Collection | Empty; add only recipient Texts |
| `subject`, `body` | Text | Empty; plain text |
| `attachments` | Collection | Empty; add only absolute file path Texts |
| `interactive` | Boolean | `False`; explicitly opt in after a user action |

```4d
var $request : cs.DraftEmail.ComposeRequest
$request:=cs.DraftEmail.ComposeRequest.new("apple-mail")
$request.to.push("recipient@example.invalid")
$request.subject:="Please review"
$request.body:="This is a draft. Send it yourself in your mail client."
$request.attachments.push("/Users/example/Documents/report.pdf")
```

The builder does not touch files, open clients or send anything. Properties are
mutable, and Collections are not element-typed in 4D; `Composer` and the CLI
therefore validate the actual values. A normal Object with the same fields is
also supported. `Composer.compose` serializes a snapshot before dispatch, so
later edits to this builder do not mutate an in-flight job.

Do not automatically set `interactive` in initialization/background code.
See [Composer](Composer.md) and the [host examples](../../../../examples/README.md).
