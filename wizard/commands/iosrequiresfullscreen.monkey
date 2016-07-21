Strict

Private

Import wizard.app
Import wizard.command
Import wizard.ios

Public

Class IosRequiresFullscreen Implements Command
    Method Run:Void(app:App)
        Ios.RequiresFullscreen()
    End
End
