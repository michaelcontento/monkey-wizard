Strict

Private

Import os
Import wizard.oshelper

Public

Class File
    Private

    Field path:String
    Field data:String = ""

    Public

    Method New(path:String)
        Self.path = path
        If FileExists(path) Then data = LoadString(path)
    End

    Method Save:Void()
        SaveString(data, path)
    End

    Method GetLine:String(line:Int)
        Return data.Split("~n")[line - 1]
    End

    Method RemoveLine:Void(line:Int)
        Local dataArr:String[] = data.Split("~n")
        data = "~n".Join(dataArr[..line - 1]) +
               "~n" +
               "~n".Join(dataArr[line..])
    End

    Method Contains:Bool(match:String)
        Return data.Contains(match)
    End

    Method Exists:Bool()
        Return FileExists(path)
    End

    Method GetPath:String()
        Return path
    End

    Method Replace:Void(match:String, text:String)
        If Not Contains(match)
            Error("Unable to find '" + match + "' within " + path)
        End

        data = data.Replace(match, text)
    End

    Method InsertAfter:Void(match:String, text:String)
        Replace(match, match + "~n" + text)
    End

    Method InsertBefore:Void(match:String, text:String)
        Replace(match, text + "~n" + match)
    End
End
