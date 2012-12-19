Strict

Private

Import os

Public

Class File
    Private

    Field name:String
    Field data:String

    Public

    Method New(name:String)
        Self.name = name
        data = LoadString(name)
    End

    Method Save:Void()
        SaveString(data, name)
    End

    Method Contains:Bool(match:String)
        Return data.Contains(match)
    End

    Method Replace:Void(match:String, text:String)
        If Not Contains(match)
            Error("Unable to find '" + match + "' within " + name)
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
