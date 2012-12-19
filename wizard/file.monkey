Strict

Private

Import os
Import wizard.helperos

Public

Class File
    Private

    Field path:String
    Field _data:String
    Field dirty:Bool
    Field loaded:Bool

    Public

    Method New(path:String)
        Self.path = path
    End

    Method Save:Void()
        If dirty
            dirty = False
            SaveString(data, path)
        End
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

    Method CopyTo:Void(dst:File)
        CopyFile(path, dst.GetPath())
    End

    Method InsertAfter:Void(match:String, text:String)
        Replace(match, match + "~n" + text)
    End

    Method InsertBefore:Void(match:String, text:String)
        Replace(match, text + "~n" + match)
    End

    Private

    Method data:String() Property
        If Not loaded And FileExists(path)
            loaded = True
            _data = LoadString(path)
        End

        Return _data
    End

    Method data:Void(newData:String) Property
        dirty = True
        _data = newData
    End
End
