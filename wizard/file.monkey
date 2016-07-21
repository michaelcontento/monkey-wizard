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

    Method FindLines:Int[](match:String)
        Local lines:String[] = data.Split("~n")
        Local result:IntList = New IntList()

        Local i:Int = 1
        For Local line:String = EachIn lines
            If line.Contains(match) Then result.AddLast(i)
            i += 1
        End

        Return result.ToArray()
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

    Method GetContentBetween:String(strStart:String, strEnd:String)
        Local posStart:Int = data.Find(strStart)
        Local posEnd:Int = data.Find(strEnd, posStart)

        If posStart = -1 Or posEnd = -1 Then Return ""
        posEnd += strEnd.Length() + 1

        Return data[posStart..posEnd]
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

    Method GetBasename:String()
        Local parts:String[] = GetPath().Split("/")
        Return parts[parts.Length() - 1]
    End

    Method CopyTo:Void(dst:File)
        CopyTo(dst.GetPath())
    End

    Method CopyTo:Void(dst:String)
        If Not FileExists(path) Then Error "File doesnt exists: " + path
        CopyFile(path, dst)
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

    Method ReplaceLine:Void(line:Int, text:String)
        Local dataArr:String[] = data.Split("~n")
        dataArr[line - 1] = text
        data = "~n".Join(dataArr)
    End

    Method InsertAfterLine:Void(line:Int, text:String)
        ReplaceLine(line, GetLine(line) + "~n" + text)
    End

    Method InsertBeforeLine:Void(line:Int, text:String)
        ReplaceLine(line, text + "~n" + GetLine(line))
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
