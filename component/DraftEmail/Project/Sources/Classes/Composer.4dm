property _cli : cs._CLI
property _target : Text

Class constructor($target : Text)
	This._target:=$target
	This._cli:=cs._CLI.new("draft-email-cli"; $target)

Function version()->$result : Object
	$result:=This._decode(This._cli.execute(New collection("--version")); "version")

Function capabilities()->$result : Object
	$result:=This._decode(This._cli.execute(New collection("capabilities")); "capabilities")

Function cleanup($token : Text)->$result : Object
	If (Not(Match regex("^[0-9a-f]{32}$"; $token)))
		return This._invalid("Cleanup requires a 32-character staging token returned by the native helper")
	End if
	$result:=This._decode(This._cli.execute(New collection("cleanup"; "--staging"; $token)); "cleanup")

Function validate($request : Object)->$result : Object
	$result:=This._checkRequest($request; False)
	If (Not($result.ok))
		return $result
	End if
	$result:=This._decode(This._cli.execute(New collection("validate"; "--request"; JSON Stringify($request))); "validate")

Function compose($request : Object)->$result : Object
	var $id; $json : Text
	var $job : Object
	var $previous : Text
	var $errors : Collection
	var CLI_LastErrors : Collection
	$result:=This._checkRequest($request; True)
	If (Not($result.ok))
		return $result
	End if
	$json:=JSON Stringify($request)
	$id:=Generate UUID
	$job:=New shared object("jobId"; $id; "state"; "pending"; "resultJSON"; "")
	$previous:=Method called on error(ek local)
	CLI_LastErrors:=Null
	ON ERR CALL("_CaptureError"; ek local)
	CALL WORKER("DraftEmail-"+$id; Formula(cs.Composer.new($1)._composeWorker($2; $3)); This._target; $json; $job)
	ON ERR CALL($previous; ek local)
	$errors:=CLI_LastErrors
	CLI_LastErrors:=Null
	If ($errors#Null)
		return New object("ok"; False; "error"; New object("code"; "worker_dispatch_failed"; "message"; "4D could not dispatch the compose worker"; "details"; $errors))
	End if
	$result:=New object("ok"; True; "jobId"; $id; "job"; $job)

Function poll($job : Object)->$result : Object
	var $state; $json; $id : Text
	If ($job=Null)
		return This._invalid("The shared job returned by compose is required")
	End if
	If (Not(OB Is shared($job)))
		return This._invalid("The shared job returned by compose is required")
	End if
	Use ($job)
		If ((Value type($job.state)=Is text) & (Value type($job.resultJSON)=Is text) & (Value type($job.jobId)=Is text))
			$state:=$job.state
			$json:=$job.resultJSON
			$id:=$job.jobId
		End if
	End use
	If (($id="") | (($state#"pending") & ($state#"running") & ($state#"finished")))
		return This._invalid("The shared job returned by compose is required")
	End if
	$result:=New object("ok"; True; "jobId"; $id; "state"; $state; "finished"; $state="finished")
	If ($state="finished")
		$result.result:=JSON Parse($json)
	End if

Function _composeWorker($json : Text; $job : Object)
	var $result : Object
	var $previous : Text
	var COMPOSE_Job : Object
	COMPOSE_Job:=$job
	$previous:=Method called on error(ek local)
	ON ERR CALL("_ComposeError"; ek local)
	Use ($job)
		$job.state:="running"
	End use
	$result:=This._decode(This._cli.execute(New collection("compose"; "--request"; $json); 600); "compose")
	ON ERR CALL($previous; ek local)
	Use ($job)
		$job.resultJSON:=JSON Stringify($result)
		$job.state:="finished"
	End use
	COMPOSE_Job:=Null
	KILL WORKER

Function _invalid($message : Text)->$result : Object
	$result:=New object("ok"; False; "error"; New object("code"; "invalid_argument"; "message"; $message))

Function _checkRequest($request : Object; $interactive : Boolean)->$result : Object
	var $field; $text : Text
	var $value : Variant
	If ($request=Null)
		return This._invalid("A request Object is required")
	End if
	If (OB Keys($request).length>9)
		return This._invalid("Unknown request fields")
	End if
	For each ($field; OB Keys($request))
		If (New collection("protocol"; "client"; "to"; "cc"; "bcc"; "subject"; "body"; "attachments"; "interactive").indexOf($field)<0)
			return This._invalid("Unknown request field: "+$field)
		End if
	End for each
	If ((Value type($request.protocol)#Is real) & (Value type($request.protocol)#Is longint))
		return This._invalid("Request protocol must be numeric 1")
	End if
	If ($request.protocol#1)
		return This._invalid("Request protocol must be numeric 1")
	End if
	If (Value type($request.client)#Is text)
		return This._invalid("Client must be apple-mail or simple-mapi")
	End if
	If (($request.client#"apple-mail") & ($request.client#"simple-mapi"))
		return This._invalid("Client must be apple-mail or simple-mapi")
	End if
	For each ($field; New collection("subject"; "body"))
		If (Value type($request[$field])#Is text)
			return This._invalid($field+" must be Text")
		End if
		$text:=$request[$field]
		If (Length($text)>16384)
			return This._invalid($field+" exceeds the request character limit")
		End if
		If (($field="subject") & (Length($text)>998))
			return This._invalid("subject exceeds the request character limit")
		End if
		If (Position(Char(0); $text; *)>0)
			return This._invalid("NUL is not permitted in "+$field)
		End if
	End for each
	For each ($field; New collection("to"; "cc"; "bcc"; "attachments"))
		If (Value type($request[$field])#Is collection)
			return This._invalid($field+" must be a Collection of Text")
		End if
		If ($request[$field].length>100)
			return This._invalid("Too many "+$field)
		End if
		If (($field="attachments") & ($request[$field].length>20))
			return This._invalid("Too many attachments")
		End if
		For each ($value; $request[$field])
			If (Value type($value)#Is text)
				return This._invalid($field+" must contain only Text")
			End if
			$text:=$value
			If (Length($text)>4096)
				return This._invalid($field+" item exceeds the request character limit")
			End if
			If (Position(Char(0); $text; *)>0)
				return This._invalid("NUL is not permitted in "+$field)
			End if
			If ($field="attachments")
				If (Not(Match regex("^(/|[A-Za-z]:[\\\\/]|\\\\\\\\).+"; $text)))
					return This._invalid("Attachments require absolute file paths")
				End if
			End if
		End for each
	End for each
	If (($request.to.length+$request.cc.length+$request.bcc.length)>100)
		return This._invalid("More than 100 recipients")
	End if
	If ($interactive)
		If (Value type($request.interactive)#Is Boolean)
			return This._invalid("compose requires interactive:true after a user action")
		End if
		If (Not($request.interactive))
			return This._invalid("compose requires interactive:true after a user action")
		End if
	End if
	$result:=New object("ok"; True)

Function _parse($text : Text)->$result : Object
	var $payload : Variant
	var $previous : Text
	var $errors : Collection
	var CLI_LastErrors : Collection
	$previous:=Method called on error(ek local)
	CLI_LastErrors:=Null
	ON ERR CALL("_CaptureError"; ek local)
	$payload:=JSON Parse($text)
	ON ERR CALL($previous; ek local)
	$errors:=CLI_LastErrors
	CLI_LastErrors:=Null
	If (($errors#Null) | (Value type($payload)#Is object))
		return New object("ok"; False; "error"; New object("code"; "invalid_protocol"; "message"; "CLI response must be a JSON object"; "details"; $errors))
	End if
	If ((Value type($payload.protocol)#Is real) & (Value type($payload.protocol)#Is longint))
		return New object("ok"; False; "error"; New object("code"; "invalid_protocol"; "message"; "Unsupported CLI protocol version"))
	End if
	If ($payload.protocol#1)
		return New object("ok"; False; "error"; New object("code"; "invalid_protocol"; "message"; "Unsupported CLI protocol version"))
	End if
	$result:=New object("ok"; True; "value"; $payload)

Function _decode($result : Object; $operation : Text)->$decoded : Object
	var $parsed; $payload : Object
	var $valid : Boolean
	If (Not($result.ok))
		If ($result.error.code="cli_error")
			$parsed:=This._parse($result.stderr)
			If ($parsed.ok)
				$payload:=$parsed.value
				If (Value type($payload.error)=Is object)
					If (Value type($payload.error.code)=Is text)
						If (Value type($payload.error.message)=Is text)
							If (($payload.error.code="") | ($payload.error.message=""))
								$result.error:=New object("code"; "invalid_protocol"; "message"; "Native error code/message must not be empty")
								return $result
							End if
							If (Value type($payload.staging)#Is undefined)
								If (Value type($payload.staging)#Is object)
									$result.error:=New object("code"; "invalid_protocol"; "message"; "Malformed staging metadata")
									return $result
								End if
								If (Not(This._validStaging($payload.staging)))
									$result.error:=New object("code"; "invalid_protocol"; "message"; "Malformed staging metadata")
									return $result
								End if
								$result.staging:=$payload.staging
							End if
							$result.error:=$payload.error
							return $result
						End if
					End if
				End if
			End if
			$result.error:=New object("code"; "invalid_protocol"; "message"; "CLI failure must include protocol 1 and an error code/message object")
		End if
		return $result
	End if
	$parsed:=This._parse($result.stdout)
	If (Not($parsed.ok))
		$result.ok:=False
		$result.error:=$parsed.error
		return $result
	End if
	$payload:=$parsed.value
	$valid:=False
	Case of
		: ($operation="version")
			If (Value type($payload.version)=Is text)
				$valid:=Match regex("^[0-9]+\\.[0-9]+\\.[0-9]+$"; $payload.version) & (OB Keys($payload).length=2)
			End if
		: ($operation="validate")
			If (Value type($payload.status)=Is text)
				$valid:=($payload.status="validated") & (OB Keys($payload).length=2)
			End if
		: ($operation="compose")
			If (Value type($payload.status)=Is text)
				$valid:=(($payload.status="handed_off") | ($payload.status="cancelled")) & (OB Keys($payload).length=2)
				If (OB Keys($payload).length=3)
					If (Value type($payload.staging)=Is object)
						$valid:=(($payload.status="handed_off") | ($payload.status="cancelled")) & This._validStaging($payload.staging)
					End if
				End if
			End if
		: ($operation="cleanup")
			If (Value type($payload.status)=Is text)
				$valid:=($payload.status="cleaned") & (OB Keys($payload).length=2)
			End if
		: ($operation="capabilities")
			$valid:=This._validCapabilities($payload)
	End case
	If (Not($valid))
		$result.ok:=False
		$result.error:=New object("code"; "invalid_protocol"; "message"; "Unexpected CLI response fields or status")
		return $result
	End if
	$result.value:=$payload
	$decoded:=$result

Function _validStaging($staging : Object)->$valid : Boolean
	var $path : Variant
	$valid:=False
	If ((Value type($staging.token)#Is text) | (Value type($staging.retained)#Is Boolean) | (Value type($staging.paths)#Is collection))
		return False
	End if
	If (OB Keys($staging).length#3)
		return False
	End if
	If ($staging.retained)
		If (Not(Match regex("^[0-9a-f]{32}$"; $staging.token)))
			return False
		End if
		If ($staging.paths.length=0)
			return False
		End if
	Else
		If (($staging.token#"") | ($staging.paths.length#0))
			return False
		End if
	End if
	For each ($path; $staging.paths)
		If (Value type($path)#Is text)
			return False
		End if
		If (Not(Match regex("^(/|[A-Za-z]:[\\\\/]|\\\\\\\\).+"; $path)))
			return False
		End if
	End for each
	$valid:=True

Function _validCapabilities($payload : Object)->$valid : Boolean
	var $field : Text
	$valid:=False
	If ((Value type($payload.backend)#Is text) | (Value type($payload.client)#Is text) | (Value type($payload.fields)#Is object))
		return False
	End if
	If ((Value type($payload.interactiveRequired)#Is Boolean) | (Value type($payload.deliveryConfirmed)#Is Boolean) | (Value type($payload.stagingCleanup)#Is Boolean))
		return False
	End if
	If ((Value type($payload.timeoutSeconds)#Is real) & (Value type($payload.timeoutSeconds)#Is longint))
		return False
	End if
	If (Not($payload.interactiveRequired) | $payload.deliveryConfirmed | Not($payload.stagingCleanup) | ($payload.timeoutSeconds#540))
		return False
	End if
	If (OB Keys($payload.fields).length#6)
		return False
	End if
	For each ($field; New collection("to"; "cc"; "bcc"; "subject"; "body"; "attachments"))
		If (Value type($payload.fields[$field])#Is Boolean)
			return False
		End if
	End for each
	If (Not($payload.fields.to) | Not($payload.fields.subject) | Not($payload.fields.body) | Not($payload.fields.attachments))
		return False
	End if
	Case of
		: ($payload.client="apple-mail")
			If (($payload.backend#"ns-sharing-service") | $payload.fields.cc | $payload.fields.bcc | (OB Keys($payload).length#8))
				return False
			End if
		: ($payload.client="simple-mapi")
			If ((Value type($payload.providerPolicy)#Is text) | (Value type($payload.newOutlook)#Is Boolean))
				return False
			End if
			If (($payload.backend#"simple-mapi-w") | Not($payload.fields.cc) | Not($payload.fields.bcc) | ($payload.providerPolicy="") | $payload.newOutlook | (OB Keys($payload).length#10))
				return False
			End if
		Else
			return False
	End case
	$valid:=True
