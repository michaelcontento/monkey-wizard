Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file

Public

Class AndroidIcons Implements Command
    Private

    Field app:App
    Field VALID_TYPES := ["low", "medium", "high", "extra-high", "super-high"]

    Public

    Method Run:Void(app:App)
        Self.app = app
        CheckArgs()

        Local dir := app.TargetDir("res/drawable-" + GetShortType() + "dpi")
        dir.Create()
        GetFile().CopyTo(dir.GetPath() + "/icon.png")
    End

    Private

    Method CheckArgs:Void()
        If Not IsValidType()
            app.LogInfo("First argument must be one of:")
            For Local type := EachIn VALID_TYPES
                app.LogInfo("- " + type)
            End
            app.LogError("Invalid first argument")
        End

        If Not IsValidFilename()
            app.LogError("Second argument must be a valid filename")
        End
    End

    Method IsValidType:Bool()
        Local given := GetType()
        For Local check := EachIn VALID_TYPES
            If given = check Then Return True
        End

        Return False
    End

    Method IsValidFilename:Bool()
        Local file := New File(GetFilename())
        Return file.Exists()
    End

    Method GetType:String()
        If app.GetAdditionArguments().Length() < 1 Then Return ""
        Return app.GetAdditionArguments()[0].ToLower()
    End

    Method GetShortType:String()
        Select GetType()
        Case "low"
            Return "l"
        Case "medium"
            Return "m"
        Case "high"
            Return "h"
        Case "extra-high"
            Return "xh"
        Case "super-high"
            Return "xxh"
        Default
            app.LogError("No short version of the given type defined")
            Return ""
        End
    End

    Method GetFilename:String()
        If app.GetAdditionArguments().Length() < 2 Then Return ""
        Return app.GetAdditionArguments()[1]
    End

    Method GetFile:File()
        Return New File(GetFilename())
    End
End
