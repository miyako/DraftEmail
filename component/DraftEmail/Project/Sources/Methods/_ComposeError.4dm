//%attributes = {"invisible":true}
var COMPOSE_Job : Object
var $errors : Collection
$errors:=Last errors
Use (COMPOSE_Job)
	COMPOSE_Job.resultJSON:=JSON Stringify(New object("ok"; False; "error"; New object("code"; "worker_error"; "message"; "Unexpected 4D compose worker error"; "details"; $errors)))
	COMPOSE_Job.state:="finished"
End use
COMPOSE_Job:=Null
KILL WORKER
ABORT
