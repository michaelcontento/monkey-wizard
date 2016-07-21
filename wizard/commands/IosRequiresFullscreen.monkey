Strict

Private

Import wizard.app
Import wizard.command
Import wizard.file
Import wizard.ios

Public

Class IosRequiresFullscreen Implements Command
    Private

    Field app:App

    Public

    Method Run:Void(app:App)
        Self.app = app
        Ios.RequiresFullscreen()
    End
End
