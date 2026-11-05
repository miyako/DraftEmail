//%attributes = {"invisible":true}
var $ignored : Integer
var $parameter; $name; $target : Text
var $filePath; $folderPath : Text
var $request; $result; $report : Object
var $email; $bad : Object
var $client : Text
var $tool : cs.DraftEmail.Composer
var $requestFile; $statusFile : 4D.File
var $renamedExecutable : 4D.File
var $argumentProbeExecutable : 4D.File
var $cases : Collection
var $invalid : Variant
var $probePaths : Variant
var $job; $snapshot : Object
var $workerName; $jobId : Text
var $attempt : Integer
var HOST_StatusFile : 4D.File
var HOST_Phase : Text
var HOST_PreviousHandler : Text
$ignored:=Get database parameter(User param value; $parameter)
If (Is Windows)
	$requestFile:=File($parameter; fk platform path)
Else
	$requestFile:=File($parameter; fk posix path)
End if
$request:=JSON Parse($requestFile.getText())
If (Is Windows)
	$statusFile:=File($request.status; fk platform path)
Else
	$statusFile:=File($request.status; fk posix path)
End if
$statusFile.setText("{\"success\":false,\"phase\":\"starting\"}")
HOST_StatusFile:=$statusFile
HOST_Phase:="constructor"
HOST_PreviousHandler:=Method called on error(ek local)
ON ERR CALL("Host_error"; ek local)
$target:="macOS-universal"
If (Is Windows)
	$target:="Windows-x64"
End if
$tool:=cs.DraftEmail.Composer.new($target)
$client:="apple-mail"
If (Is Windows)
	$client:="simple-mapi"
End if
$email:=cs.DraftEmail.ComposeRequest.new($client)
$email.to.push("recipient@example.invalid")
$cases:=New collection("world"; "path with spaces"; "quote \" and \\ slash"; "line\nbreak"; "日本語"; "$() ; & % !")
$report:=New object("success"; True; "finished"; False; "cases"; New collection)
$report.compiled:=$tool._cli.compiled
HOST_Phase:="renamed executable"
If (Is Windows)
	$renamedExecutable:=File($request.executable; fk platform path)
Else
	$renamedExecutable:=File($request.executable; fk posix path)
End if
$report.renamedExecutable:=cs.DraftEmail._Process.new().execute($renamedExecutable; New collection("--version"))
If (Not($report.renamedExecutable.ok))
	$report.success:=False
End if
HOST_Phase:="feature cases"
If ($request.expectCompiled#$report.compiled)
	$report.success:=False
End if
For each ($name; $cases)
	$email.subject:="Draft from 4D"
	$email.body:=$name
	$result:=$tool.validate($email)
	$report.cases.push($result)
	If (Not($result.ok))
		$report.success:=False
	Else
		If ($result.value.status#"validated")
			$report.success:=False
		End if
	End if
End for each
$bad:=JSON Parse(JSON Stringify($email))
$bad.subject:=Null
$result:=$tool.validate($bad)
$report.invalid:=$result
If ($result.ok | ($result.error.code#"invalid_argument"))
	$report.success:=False
End if
$report.noInteractive:=$tool.compose($email)
$report.nullRequest:=$tool.validate(Null)
$report.nullCompose:=$tool.compose(Null)
$report.pollInvalid:=$tool.poll(New object)
If ($report.noInteractive.ok | $report.nullRequest.ok | $report.nullCompose.ok | $report.pollInvalid.ok)
	$report.success:=False
End if
$report.requestTypes:=New collection
For each ($invalid; New collection(True; False; Null; "1"; 2))
	$bad:=JSON Parse(JSON Stringify($email))
	$bad.protocol:=$invalid
	$report.requestTypes.push($tool.validate($bad))
End for each
For each ($invalid; New collection(123; True; Null; New collection; New object))
	$bad:=JSON Parse(JSON Stringify($email))
	$bad.body:=$invalid
	$report.requestTypes.push($tool.validate($bad))
End for each
$bad:=JSON Parse(JSON Stringify($email))
$bad.to:=New collection(123)
$report.requestTypes.push($tool.validate($bad))
$bad:=JSON Parse(JSON Stringify($email))
$bad.attachments:=New collection("relative.pdf")
$report.requestTypes.push($tool.validate($bad))
$bad:=JSON Parse(JSON Stringify($email))
$bad.body:="prefix"+Char(0)+"suffix"
$report.requestTypes.push($tool.validate($bad))
For each ($result; $report.requestTypes)
	If ($result.ok | ($result.error.code#"invalid_argument"))
		$report.success:=False
	End if
End for each
$bad:=JSON Parse(JSON Stringify($email))
$bad.client:="apple-mail"
$bad.cc.push("copy@example.invalid")
$report.nativeInvalid:=$tool.validate($bad)
If ($report.nativeInvalid.ok | ($report.nativeInvalid.error.code#"unsupported_field") | ($report.nativeInvalid.exitCode#2))
	$report.success:=False
End if
$report.capabilities:=$tool.capabilities()
$report.capabilitiesClientMatches:=False
If ($report.capabilities.ok)
	$report.capabilitiesClientMatches:=($report.capabilities.value.client=$client)
End if
If (Not($report.capabilitiesClientMatches))
	$report.success:=False
End if
$result:=$tool.version()
$report.version:=$result
If (Not($result.ok))
	$report.success:=False
Else
	If ($result.value.version#"0.1.0")
		$report.success:=False
	End if
End if
HOST_Phase:="missing executable"
$result:=cs.DraftEmail._CLI.new("missing-cli"; $target).execute(New collection("--version"))
$report.missing:=$result
If ($result.error.code#"missing_executable")
	$report.success:=False
End if
$result:=cs.DraftEmail.Composer.new("unsupported-arm-target").version()
$report.unsupported:=$result
If ($result.error.code#"unsupported_architecture")
	$report.success:=False
End if
HOST_Phase:="launch failure"
$result:=cs.DraftEmail._Process.new().execute($requestFile; New collection)
$report.launch:=$result
If ($result.error.code#"launch_failed")
	$report.success:=False
End if
HOST_Phase:="malformed protocol"
$result:=$tool._decode(New object("ok"; True; "stdout"; "not json"; "stderr"; ""; "exitCode"; 0); "validate")
$report.protocol:=$result
If ($result.error.code#"invalid_protocol")
	$report.success:=False
End if
$report.protocolTypes:=New collection
For each ($name; New collection("{\"protocol\":true,\"status\":\"validated\"}"; "{\"protocol\":false,\"status\":\"validated\"}"))
	$result:=$tool._decode(New object("ok"; True; "stdout"; $name; "stderr"; ""; "exitCode"; 0); "validate")
	$report.protocolTypes.push($result)
	If ($result.ok | ($result.error.code#"invalid_protocol"))
		$report.success:=False
	End if
End for each
$result:=$tool._decode(New object("ok"; True; "stdout"; "{\"protocol\":null,\"status\":\"validated\"}"; "stderr"; ""; "exitCode"; 0); "validate")
$report.protocolNull:=$result
If ($result.ok | ($result.error.code#"invalid_protocol"))
	$report.success:=False
End if
$report.protocolScalars:=New collection
For each ($name; New collection("true"; "false"; "null"; "1"; "[]"))
	$result:=$tool._decode(New object("ok"; True; "stdout"; $name; "stderr"; ""; "exitCode"; 0); "validate")
	$report.protocolScalars.push($result)
	If ($result.ok | ($result.error.code#"invalid_protocol"))
		$report.success:=False
	End if
End for each
$report.protocolStatuses:=New collection
For each ($name; New collection("{\"protocol\":2,\"status\":\"validated\"}"; "{\"protocol\":1,\"status\":\"sent\"}"; "{\"protocol\":1,\"status\":null}"; "{\"protocol\":1,\"status\":true}"; "{\"protocol\":1}"))
	$result:=$tool._decode(New object("ok"; True; "stdout"; $name; "stderr"; ""; "exitCode"; 0); "validate")
	$report.protocolStatuses.push($result)
	If ($result.ok | ($result.error.code#"invalid_protocol"))
		$report.success:=False
	End if
End for each
$report.composeStatuses:=New collection
For each ($name; New collection("handed_off"; "cancelled"; "sent"))
	$result:=$tool._decode(New object("ok"; True; "stdout"; JSON Stringify(New object("protocol"; 1; "status"; $name)); "stderr"; ""; "exitCode"; 0); "compose")
	$report.composeStatuses.push($result)
End for each
$report.stagingProtocol:=New collection
For each ($name; New collection("{\"protocol\":1,\"status\":\"handed_off\",\"staging\":{\"token\":\"\",\"paths\":[],\"retained\":false}}"; "{\"protocol\":1,\"status\":\"handed_off\",\"staging\":{\"token\":true,\"paths\":[],\"retained\":false}}"; "{\"protocol\":1,\"status\":\"handed_off\",\"staging\":{\"token\":\"\",\"paths\":[],\"retained\":true}}"))
	$result:=$tool._decode(New object("ok"; True; "stdout"; $name; "stderr"; ""; "exitCode"; 0); "compose")
	$report.stagingProtocol.push($result)
End for each
$report.nativeError:=$tool._decode(New object("ok"; False; "stdout"; ""; "stderr"; "{\"protocol\":1,\"error\":{\"code\":\"backend_unavailable\",\"message\":\"Test refusal\",\"details\":{\"backend\":\"test\"}}}\n"; "exitCode"; 3; "error"; New object("code"; "cli_error"; "message"; "native failure")); "compose")
$report.malformedError:=$tool._decode(New object("ok"; False; "stdout"; ""; "stderr"; "{\"protocol\":true,\"error\":{\"code\":\"wrong\",\"message\":\"wrong\"}}"; "exitCode"; 3; "error"; New object("code"; "cli_error"; "message"; "native failure")); "compose")
$report.stagedError:=$tool._decode(New object("ok"; False; "stdout"; ""; "stderr"; "{\"protocol\":1,\"error\":{\"code\":\"backend_error\",\"message\":\"Test refusal\"},\"staging\":{\"token\":\"0123456789abcdef0123456789abcdef\",\"paths\":[\"/owned/copy.txt\"],\"retained\":true}}\n"; "exitCode"; 3; "error"; New object("code"; "cli_error"; "message"; "native failure")); "compose")
$report.cleanupInvalid:=$tool.cleanup("../not-a-token")
HOST_Phase:="safe worker prelaunch refusal"
$report.workerResults:=New collection
$report.workerCleanup:=True
For each ($name; New collection("first"; "second"))
	$jobId:=Generate UUID
	$workerName:="DraftEmail-test-"+$jobId
	$job:=New shared object("jobId"; $jobId; "state"; "pending"; "resultJSON"; "")
	// An unsupported target guarantees no native composer can be launched.
	CALL WORKER($workerName; Formula(cs.DraftEmail.Composer.new("unsupported-test-target")._composeWorker($1; $2)); "{}"; $job)
	$snapshot:=$tool.poll($job)
	For ($attempt; 1; 300)
		If ($snapshot.finished)
			break
		End if
		DELAY PROCESS(Current process; 1)
		$snapshot:=$tool.poll($job)
	End for
	$report.workerResults.push($snapshot)
	If (Not($snapshot.finished))
		$report.success:=False
	End if
	If ($snapshot.jobId#$jobId)
		$report.success:=False
	End if
	For ($attempt; 1; 60)
		If (Process number($workerName)=0)
			break
		End if
		DELAY PROCESS(Current process; 1)
	End for
	If (Process number($workerName)#0)
		$report.workerCleanup:=False
		$report.success:=False
		KILL WORKER($workerName)
	End if
End for each
HOST_Phase:="File argument"
$report.fileTypedArgument:=cs.DraftEmail._CLI.new("draft-email-cli"; $target).execute(New collection("validate"; "--request"; $requestFile))
If ($report.fileTypedArgument.ok | ($report.fileTypedArgument.exitCode#2))
	$report.success:=False
Else
	If (JSON Parse($report.fileTypedArgument.stderr).error.code#"invalid_json")
		$report.success:=False
	End if
End if
$email.subject:=$parameter
$email.body:=$requestFile.path
$result:=$tool.validate($email)
$report.fileArgument:=$result
If (Not($result.ok))
	$report.success:=False
Else
	If (JSON Parse($result.stdout).status#"validated")
		$report.success:=False
	End if
End if
$report.rejectedArguments:=New collection
HOST_Phase:="scalar arguments"
For each ($invalid; New collection(123; 1.25; True; False; Null; Current date; Current time; New collection; New object))
	$result:=cs.DraftEmail._Process.new().execute($requestFile; New collection($invalid))
	$report.rejectedArguments.push(New object("type"; Value type($invalid); "result"; $result))
	If ($result.ok | ($result.error.code#"invalid_argument"))
		$report.success:=False
	End if
End for each
$report.rejectedNul:=New collection
HOST_Phase:="NUL arguments"
For each ($name; New collection(Char(0)+"prefix"; "middle"+Char(0)+"suffix"; "trailing"+Char(0)))
	$result:=cs.DraftEmail._Process.new().execute($requestFile; New collection($name))
	$report.rejectedNul.push($result)
	If ($result.ok | ($result.error.code#"invalid_argument"))
		$report.success:=False
	End if
End for each
$report.missingFile:=cs.DraftEmail._Process.new().execute(Null; New collection)
$report.missingArguments:=cs.DraftEmail._Process.new().execute($requestFile; Null)
If ($report.missingFile.ok | ($report.missingFile.error.code#"invalid_argument"))
	$report.success:=False
End if
If ($report.missingArguments.ok | ($report.missingArguments.error.code#"invalid_argument"))
	$report.success:=False
End if
HOST_Phase:="Folder argument"
$report.folderTypedArgument:=cs.DraftEmail._CLI.new("draft-email-cli"; $target).execute(New collection("validate"; "--request"; $requestFile.parent))
If ($report.folderTypedArgument.ok | ($report.folderTypedArgument.exitCode#2))
	$report.success:=False
Else
	If (JSON Parse($report.folderTypedArgument.stderr).error.code#"invalid_json")
		$report.success:=False
	End if
End if
$folderPath:=$requestFile.parent.path
If (Is Windows)
	$folderPath:=$requestFile.parent.platformPath
End if
$email.subject:=$folderPath
$email.body:=$folderPath
$result:=$tool.validate($email)
$report.folderArgument:=$result
If (Not($result.ok))
	$report.success:=False
Else
	If (JSON Parse($result.stdout).status#"validated")
		$report.success:=False
	End if
End if
HOST_Phase:="exact typed argument roundtrip"
If (Is Windows)
	$argumentProbeExecutable:=File($request.argumentProbeExecutable; fk platform path)
	$filePath:=$requestFile.platformPath
	$folderPath:=$requestFile.parent.platformPath
Else
	$argumentProbeExecutable:=File($request.argumentProbeExecutable; fk posix path)
	$filePath:=$requestFile.path
	$folderPath:=$requestFile.parent.path
End if
$report.argumentProbe:=cs.DraftEmail._Process.new().execute($argumentProbeExecutable; New collection("-X"; "utf8"; "-c"; "import json,sys; print(json.dumps(sys.argv[1:],ensure_ascii=True))"; $requestFile; $requestFile.parent))
$report.typedPathsExact:=False
$report.typedPathsExpected:=New collection($filePath; $folderPath)
If ($report.argumentProbe.ok)
	$probePaths:=JSON Parse($report.argumentProbe.stdout)
	$report.typedPathsActual:=$probePaths
	If (Value type($probePaths)=Is collection)
		If ($probePaths.length=2)
			If ((Value type($probePaths[0])=Is text) & (Value type($probePaths[1])=Is text))
				$report.typedPathsExact:=(Compare strings($probePaths[0]; $filePath; sk char codes)=0) & (Compare strings($probePaths[1]; $folderPath; sk char codes)=0)
			End if
		End if
	End if
End if
If (Not($report.typedPathsExact))
	$report.success:=False
End if
$report.finished:=True
ON ERR CALL(HOST_PreviousHandler; ek local)
$statusFile.setText(JSON Stringify($report))
HOST_StatusFile:=Null
HOST_Phase:=""
HOST_PreviousHandler:=""
QUIT 4D
