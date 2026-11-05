property protocol : Integer
property client : Text
property to : Collection
property cc : Collection
property bcc : Collection
property subject : Text
property body : Text
property attachments : Collection
property interactive : Boolean

Class constructor($client : Text)
	This.protocol:=1
	This.client:=$client
	This.to:=New collection
	This.cc:=New collection
	This.bcc:=New collection
	This.subject:=""
	This.body:=""
	This.attachments:=New collection
	This.interactive:=False
