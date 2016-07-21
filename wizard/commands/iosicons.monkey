Strict

Private

Import wizard.app
Import wizard.command
Import wizard.file
Import wizard.ios

Public

Class IosIcons Implements Command
    Private

    Field app:App

    Public

    Method Run:Void(app:App)
        Self.app = app

        Local prerendered := IsPrerendered()
        Local icons := GetNewIcons()
        CheckNewIconsExists(icons)

        RemovePrerenderedFlag()
        RemoveIcons()

        AddPrerenderedFlag(prerendered)
        AddIcons(icons, prerendered)
        CopyIconFiles(icons)
    End

    Private

    Method CopyIconFiles:Void(icons:File[])
        Local dstDir := app.TargetDir("").GetPath()
        For Local i := 0 Until icons.Length()
            icons[i].CopyTo(dstDir + icons[i].GetBasename())
        End
    End

    Method CheckNewIconsExists:Void(icons:File[])
        For Local i := 0 Until icons.Length()
            If icons[i].Exists() Then Continue
            app.LogError("Invalid file given: " + icons[i].GetPath())
        End
    End

    Method AddIcons:Void(icons:File[], isPrerendered:Bool)
        AddIconsPlist(icons, isPrerendered)
        AddIconsDefinitions(icons)
    End

    Method AddIconsDefinitions:Void(icons:File[])
        For Local i := 0 Until icons.Length()
            Local filename := icons[i].GetBasename()
            Local firstId := Ios.GenerateUniqueId()
            Local secondId := Ios.GenerateUniqueId()

            Ios.AddIconPBXBuildFile(filename, firstId, secondId)
            Ios.AddIconPBXFileReference(filename, secondId)
            Ios.AddIconPBXGroup(filename, secondId)
            Ios.AddIconPBXResourcesBuildPhase(filename, firstId)
        End
    End

    Method AddIconsPlist:Void(icons:File[], isPrerendered:Bool)
        If icons.Length() = 0 Then Return

        Local files := ""
        For Local i := 0 Until icons.Length()
            files += "~t~t~t~t<string>" + icons[i].GetBasename() + "</string>~n"
        End

        Local prerendered := ""
        If isPrerendered
            prerendered = "~t~t~t<key>UIPrerenderedIcon</key>~n~t~t~t<true/>~n"
        End

        Local file := Ios.GetPlist()
        file.InsertBefore(
            "</dict>~n</plist>",
            "~t<key>CFBundleIconFiles</key>~n" +
            "~t<array>~n" +
            files +
            "~t</array>~n" +
            "~t<key>CFBundleIcons</key>~n" +
            "~t<dict>~n" +
            "~t~t<key>CFBundlePrimaryIcon</key>~n" +
            "~t~t<dict>~n" +
            "~t~t~t<key>CFBundleIconFiles</key>~n" +
            "~t~t~t<array>~n" +
            files +
            "~t~t~t</array>~n" +
            prerendered +
            "~t~t</dict>~n" +
            "~t</dict>")
    End

    Method AddPrerenderedFlag:Void(enabled:Bool)
        If Not enabled Then Return

        Local file := Ios.GetPlist()
        file.InsertBefore(
            "</dict>~n</plist>",
            "~t<key>UIPrerenderedIcon</key>~n" +
            "~t<true/>")
    End

    Method RemovePrerenderedFlag:Void()
        Local file := Ios.GetPlist()
        Local lines := file.FindLines("<key>UIPrerenderedIcon</key>")

        For Local i := lines.Length() - 1 To 0 Step -1
            file.RemoveLine(lines[i] + 1)
            file.RemoveLine(lines[i])
        End
    End

    Method RemoveIcons:Void()
        Local removedRows := RemoveKeyWithValues("CFBundleIconFiles")
        ParseRowsAndRemoveFiles(removedRows)

        ' Can occur twice because this setting is valid as stand alone key
        ' (< iOS 5) and inside the new (> iOS 5) CFBundleIcons
        removedRows = RemoveKeyWithValues("CFBundleIconFiles")
        ParseRowsAndRemoveFiles(removedRows)

        RemoveKeyWithValues("CFBundlePrimaryIcon")
        RemoveKeyWithValues("CFBundleIcons")
    End

    Method ParseRowsAndRemoveFiles:Void(removed:String[])
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

    Method IsPrerendered:Bool()
        If app.GetAdditionArguments().Length() < 1
            app.LogError("First argument missing - prerendered or not?")
        End

        Local value := app.GetAdditionArguments()[0]
        If value <> "0" And value <> "1"
            app.LogError("Invalid prerendered value given. Valid modes are 0 and 1.")
        End

        Return (value = "1")
    End

    Method GetNewIcons:File[]()
        If app.GetAdditionArguments().Length() < 2
            app.LogError("No icon arguments found")
        End

        Local result := New File[app.GetAdditionArguments().Length() - 1]
        For Local i := 1 Until app.GetAdditionArguments().Length()
            result[i - 1] = New File(app.GetAdditionArguments()[i])
        End
        Return result
    End
End
