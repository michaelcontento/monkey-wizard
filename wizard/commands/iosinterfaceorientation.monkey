Strict

Private

Import wizard.app
Import wizard.command
Import wizard.ios

Public

Class IosInterfaceOrientation Implements Command
    Private

    Field app:App
    Const BOTH := "BOTH"
    Const LANDSCAPE := "LANDSCAPE"
    Const PORTRAIT := "PORTRAIT"

    Public

    Method Run:Void(app:App)
        Self.app = app

        Select GetOrientation()
        Case BOTH
            RemoveOldSettings()
            AddOrientationBoth()
        Case LANDSCAPE
            RemoveOldSettings()
            AddOrientationLandscape()
        Case PORTRAIT
            RemoveOldSettings()
            AddOrientationPortrait()
        Default
            app.LogError("Invalid orientation given")
        End
    End

    Private

    Method RemoveOldSettings:Void()
        Local file := Ios.GetPlist()
        Local lines := file.FindLines("<key>UISupportedInterfaceOrientations")

        For Local i := lines.Length() - 1 To 0 Step -1
            RemoveKeyWithValues(lines[i])
        End
    End

    Method RemoveKeyWithValues:Void(startLine:Int)
        Local file := Ios.GetPlist()
        Local rows := 0

        Repeat
            rows += 1
        Until file.GetLine(startLine + rows).Trim().StartsWith("</")

        If rows <= 0 Then Return

        For Local i := rows To 0 Step -1
            file.RemoveLine(startLine + i)
        End
    End

    Method AddOrientationBoth:Void()
        Local file := Ios.GetPlist()
        file.InsertBefore(
            "</dict>",
            "~t<key>UISupportedInterfaceOrientations</key>~n" +
            "~t<array>~n" +
            "~t~t<string>UIInterfaceOrientationPortrait</string>~n" +
            "~t~t<string>UIInterfaceOrientationPortraitUpsideDown</string>~n" +
            "~t~t<string>UIInterfaceOrientationLandscapeLeft</string>~n" +
            "~t~t<string>UIInterfaceOrientationLandscapeRight</string>~n" +
            "~t</array>~n" +
            "~t<key>UISupportedInterfaceOrientations~~ipad</key>~n" +
            "~t<array>~n" +
            "~t~t<string>UIInterfaceOrientationPortrait</string>~n" +
            "~t~t<string>UIInterfaceOrientationPortraitUpsideDown</string>~n" +
            "~t~t<string>UIInterfaceOrientationLandscapeLeft</string>~n" +
            "~t~t<string>UIInterfaceOrientationLandscapeRight</string>~n" +
            "~t</array>")
    End

    Method AddOrientationLandscape:Void()
        Local file := Ios.GetPlist()
        file.InsertBefore(
            "</dict>",
            "~t<key>UISupportedInterfaceOrientations</key>~n" +
            "~t<array>~n" +
            "~t~t<string>UIInterfaceOrientationLandscapeLeft</string>~n" +
            "~t~t<string>UIInterfaceOrientationLandscapeRight</string>~n" +
            "~t</array>~n" +
            "~t<key>UISupportedInterfaceOrientations~~ipad</key>~n" +
            "~t<array>~n" +
            "~t~t<string>UIInterfaceOrientationLandscapeLeft</string>~n" +
            "~t~t<string>UIInterfaceOrientationLandscapeRight</string>~n" +
            "~t</array>")
    End

    Method AddOrientationPortrait:Void()
        Local file := Ios.GetPlist()
        file.InsertBefore(
            "</dict>",
            "~t<key>UISupportedInterfaceOrientations</key>~n" +
            "~t<array>~n" +
            "~t~t<string>UIInterfaceOrientationPortrait</string>~n" +
            "~t~t<string>UIInterfaceOrientationPortraitUpsideDown</string>~n" +
            "~t</array>~n" +
            "~t<key>UISupportedInterfaceOrientations~~ipad</key>~n" +
            "~t<array>~n" +
            "~t~t<string>UIInterfaceOrientationPortrait</string>~n" +
            "~t~t<string>UIInterfaceOrientationPortraitUpsideDown</string>~n" +
            "~t</array>")
    End

    Method GetOrientation:String()
        If app.GetAdditionArguments().Length() <> 1
            app.LogError("Orientation argument missing")
        End

        Return app.GetAdditionArguments()[0].ToUpper()
    End
End
