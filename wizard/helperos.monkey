Strict

Private

Import os

Public

Function FileExists:Bool(name:String)
    If FileType(name) = FILETYPE_FILE Then Return True
    Return False
End

Function DirExists:Bool(name:String)
    If FileType(name) = FILETYPE_DIR Then Return True
    Return False
End
