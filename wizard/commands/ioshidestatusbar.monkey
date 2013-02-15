Strict

Private

Import wizard.app
Import wizard.command
Import wizard.ios

Public

Class IosHideStatusBar Implements Command
    Private

    Field app:App

    Public

    Method Run:Void(app:App)
        Self.app = app

        RemoveOldSettings()
        If IsEnabled() Then AddSettings()
    End

    Private

    Method RemoveOldSettings:Void()
        Local file := Ios.GetPlist()
        Local lines := file.FindLines("<key>UIStatusBarHidden")

        For Local i := lines.Length() - 1 To 0 Step -1
            file.RemoveLine(lines[i] + 1)
            file.RemoveLine(lines[i])
        End
    End

    Method AddSettings:Void()
        Local file := Ios.GetPlist()
        file.InsertBefore(
            "</dict>",
            "~t<key>UIStatusBarHidden</key>~n" +
            "~t<true/>~n" +
            "~t<key>UIStatusBarHidden~~ipad</key>~n" +
            "~t<true/>")
    End

    Method IsEnabled:Bool()
        If app.GetAdditionArguments().Length() <> 1
            app.LogError("Mode argument (valid values 0/1) missing")
        End

        Local value := app.GetAdditionArguments()[0]
        If value <> "0" And value <> "1"
            app.LogError("Invalid mode given. Valid modes are 0 and 1.")
        End

        Return (value = "1")
    End
End
