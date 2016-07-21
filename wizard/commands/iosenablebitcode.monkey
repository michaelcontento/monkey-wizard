Strict

Private

Import wizard.app
Import wizard.command
Import wizard.ios

Public

Class IosEnableBitcode Implements Command
    Private

    Field app:App

    Public

    Method Run:Void(app:App)
        Self.app = app
        Ios.SetBitcode(GetBitcodeState())
    End

    Method GetBitcodeState:String()
        If app.GetAdditionArguments().Length() <> 1
            app.LogError("Argument missing. Set to YES or NO")
        End

        Return app.GetAdditionArguments()[0].ToUpper()
    End
End
