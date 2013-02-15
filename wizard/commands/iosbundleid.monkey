Strict

Private

Import wizard.app
Import wizard.command
Import wizard.ios

Public

Class IosBundleId Implements Command
    Method Run:Void(app:App)
        Ios.UpdatePlistSetting(app, "CFBundleIdentifier", GetBundleId(app))
    End

    Private

    Function GetBundleId:String(app:App)
        If app.GetAdditionArguments().Length() <> 1
            app.LogError("Bundle id argument missing")
        End

        Return app.GetAdditionArguments()[0]
    End
End
