Strict

Private

Import wizard.app
Import wizard.file

Public

Class Android Abstract
    Function GetManifest:File(app:App)
        Return app.TargetFile("templates/AndroidManifest.xml")
    End

    Function AddPermission:Void(app:App, permission:String)
        Local match:String = "<application "
        Local patchStr:String = "<uses-permission " +
            "android:name=~q" + permission + "~q />"
        Local target:File = Android.GetManifest(app)

        If target.Contains(patchStr) Then Return

        If Not target.Contains(match)
            app.LogWarning("Unable to add required permission to AndroidManifest.xml")
            app.LogWarning("Please add the following permission manually:")
            app.LogWarning("    " + patchStr)
        Else
            target.InsertBefore(match, patchStr)
        End
    End

    Function EnsureLibsFolder:Void(app:App)
        app.TargetDir("libs").Create()
    End
End

