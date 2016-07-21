Strict

Private

Import wizard.android
Import wizard.app
Import wizard.command

Public

Class AndroidPermissionRemove Implements Command
    Method Run:Void(app:App)
        Local removed := Android.RemovePermission(GetArgument(app))
        If removed = 0
            app.LogError("Unable to remove given permission")
        End
    End

    Private

    Method GetArgument:String(app:App)
        If app.GetAdditionArguments().Length() < 1
            app.LogError("Permission argument missing")
            Return ""
        End

        Return app.GetAdditionArguments()[0]
    End
End
