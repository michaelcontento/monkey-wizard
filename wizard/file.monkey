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

    Method ContainsBetween:Bool(match:String, strStart:String, strEnd:String)
        Local posStart:Int = data.Find(strStart)
        Local posEnd:Int = data.Find(strEnd, posStart)

        If posStart = -1 Or posEnd = -1 Then Return False

        ' Adjust posEnd to include the end pattern
        posEnd += strEnd.Length() + 1

        Return data[posStart..posEnd].Contains(match)
    End

    Method Exists:Bool()
        Return FileExists(path)
    End

    Method Remove:Void()
        DeleteFile(path)
    End

    Method GetPath:String()
        Return path
    End

    Method CopyTo:Void(dst:File)
        CopyFile(path, dst.GetPath())
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

    Method ReplaceBetween:Void(match:String, text:String, strStart:String, strEnd:String)
        If Not ContainsBetween(match, strStart, strEnd)
            Error("Unable to find '" + match + "' within " + path + "~n" +
                "and the given range that:~n" +
                "starts with: " + strStart + "~n" +
                "  ends with: " + strEnd)
        End

        Local posStart:Int = data.Find(strStart)
        Local posEnd:Int = data.Find(strEnd, posStart)

        ' Adjust posEnd to include the end pattern
        posEnd += strEnd.Length() + 1

        Local replaceData:String = data[posStart..posEnd]
        replaceData = replaceData.Replace(match, text)

        data = data[..posStart] + replaceData + data[posEnd..]
    End

    Method InsertAfterBetween:Void(match:String, text:String, strStart:String, strEnd:String)
        ReplaceBetween(match, match + "~n" + text, strStart, strEnd)
    End

    Method InsertBeforeBetween:Void(match:String, text:String, strStart:String, strEnd:String)
        ReplaceBetween(match, text + "~n" + match, strStart, strEnd)
    End

    Method Set:Void(text:String)
        data = text
    End

    Method Get:String()
        Return data
    End

    Method Append:Void(text:String)
        data += text
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
