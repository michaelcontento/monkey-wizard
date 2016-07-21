Strict

Private

Import wizard.app
Import wizard.command
Import wizard.ios
Import wizard.file

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

        RemoveOldSettings()

        Local src:File = Ios.GetMainSource()

        ' remove deprecated code
        Local lines := src.FindLines("-(NSUInteger)supportedInterfaceOrientations")
        For Local i := 0 To 12
            src.RemoveLine(lines[0]+1)
        Next

        Local code$
        Select GetOrientation()
            Case BOTH
                AddOrientationBothPlist()
                code = AddOrientationBoth()
            Case LANDSCAPE
                AddOrientationLandscapePlist()
                code = AddOrientationLandscape()
            Case PORTRAIT
                AddOrientationPortraitPlist()
                code = AddOrientationPortrait()
            Default
                app.LogError("Invalid orientation given")
        End

        src.InsertAfterLine(lines[0], "~t" + code)
    End

    Private

    Method AddOrientationBoth:String()
        Return "NSUInteger mask = UIInterfaceOrientationMaskLandscapeLeft|UIInterfaceOrientationMaskLandscapeRight|UIInterfaceOrientationMaskPortrait|UIInterfaceOrientationMaskPortraitUpsideDown;"
    End

    Method AddOrientationLandscape:String()
        Return "NSUInteger mask = UIInterfaceOrientationMaskLandscapeLeft|UIInterfaceOrientationMaskLandscapeRight;"
    End

    Method AddOrientationPortrait:String()
        Return "NSUInteger mask = UIInterfaceOrientationMaskPortrait|UIInterfaceOrientationMaskPortraitUpsideDown;"
    End

    Method GetOrientation:String()
        If app.GetAdditionArguments().Length() <> 1
            app.LogError("Orientation argument missing")
        End

        Return app.GetAdditionArguments()[0].ToUpper()
    End

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

    Method AddOrientationBothPlist:Void()
        Local file := Ios.GetPlist()
        file.InsertBefore(
            "</dict>~n</plist>",
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

    Method AddOrientationLandscapePlist:Void()
        Local file := Ios.GetPlist()
        file.InsertBefore(
            "</dict>~n</plist>",
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

    Method AddOrientationPortraitPlist:Void()
        Local file := Ios.GetPlist()
        file.InsertBefore(
            "</dict>~n</plist>",
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
End
