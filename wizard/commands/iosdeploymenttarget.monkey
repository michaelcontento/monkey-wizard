Strict

Private

Import wizard.app
Import wizard.command
Import wizard.ios

Public

Class IosDeploymentTarget Implements Command
    Method Run:Void(app:App)
        Ios.UpdateDeploymentTarget(GetTarget(app))
    End

    Private

    Method GetTarget:String(app:App)
        If app.GetAdditionArguments().Length() <> 1
            app.LogError("Target version argument missing")
        End

        Return app.GetAdditionArguments()[0]
    End
End
