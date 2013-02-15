Strict

Private

Import wizard.app
Import wizard.command
Import wizard.ios

Public

Class IosCompressPngFiles Implements Command
    Private

    Field app:App

    Public

    Method Run:Void(app:App)
        Self.app = app

        Local enabled := IsEnabled()
        RemoveOldSettings()
        AddSettings(enabled)
    End

    Private

    Method RemoveOldSettings:Void()
        Local file := Ios.GetProject()
        Local lines := file.FindLines("COMPRESS_PNG_FILES = ")

        For Local i := lines.Length() - 1 To 0 Step -1
            file.RemoveLine(lines[i])
        End
    End

    Method AddSettings:Void(enabled:Bool)
        Local value := "NO"
        If enabled Then value = "YES"

        Local file := Ios.GetProject()
        Local lines := file.FindLines("buildSettings = {")

        Local newRow := "~t~t~t~tCOMPRESS_PNG_FILES = " + value + ";"
        For Local i := lines.Length() - 1 To 0 Step -1
            file.InsertAfterLine(lines[i], newRow)
        End
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
