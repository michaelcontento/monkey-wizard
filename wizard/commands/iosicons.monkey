Strict

Private

Import wizard.app
Import wizard.command
Import wizard.ios

Public

Class IosIcons Implements Command
    Private

    Field app:App

    Public

    Method Run:Void(app:App)
        Self.app = app

        RemovePrerenderedFlag()
        RemoveIcons()
    End

    Private

    Method RemovePrerenderedFlag:Void()
        Local file := Ios.GetPlist()
        Local lines := file.FindLines("<key>UIPrerenderedIcon</key>")

        For Local i := lines.Length() - 1 To 0 Step -1
            file.RemoveLine(lines[i] + 1)
            file.RemoveLine(lines[i])
        End
    End

    Method RemoveIcons:Void()
        Local removed := RemoveKeyWithValues("CFBundleIconFiles")
        RemoveKeyWithValues("CFBundlePrimaryIcon")
        RemoveKeyWithValues("CFBundleIcons")

        For Local i := 0 Until removed.Length()
            Local row := removed[i].Trim()
            If Not row.StartsWith("<string>") Then Continue

            Local filename := ExtractFileName(row)
            RemoveFileDefinition(filename)
            RemoveFilePhysical(filename)
        End
    End

    Method RemoveKeyWithValues:String[](key:String)
        ' --- Init
        Local file := Ios.GetPlist()
        Local lines := file.FindLines("<key>" + key + "</key>")
        If lines.Length() = 0 Then Return []

        ' --- Count rows that belongs to this key
        Local startLine := lines[0]
        Local rows := 0

        Repeat
            rows += 1
        Until file.GetLine(startLine + rows).Trim().StartsWith("</")

        If rows <= 0 Then Return []

        ' --- Remove all rows
        Local removed := New String[rows + 1]
        For Local i := rows To 0 Step -1
            removed[i] = file.GetLine(startLine + i)
            file.RemoveLine(startLine + i)
        End

        Return removed
    End

    Method ExtractFileName:String(row:String)
        Return row.Replace("<string>", "").Replace("</string>", "")
    End

    Method RemoveFileDefinition:Void(filename:String)
        If filename = "" Then Return

        Local file := Ios.GetProject()
        Local lines := file.FindLines("/* " + filename)

        For Local i := lines.Length() - 1 To 0 Step -1
            file.RemoveLine(lines[i])
        End
    End

    Method RemoveFilePhysical:Void(filename:String)
        Local file := app.TargetFile(filename)
        If file.Exists() Then file.Remove()
    End
End
