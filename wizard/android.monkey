Strict

Private

Import wizard.app
Import wizard.file

Public

Class Android Abstract
    Global app:App

    Function GetManifest:File()
        Return app.TargetFile("templates/AndroidManifest.xml")
    End

    Function RemovePermission:Int(permission:String)
        Local searchStr:String = "uses-permission android:name=~q" + permission + "~q"

        Local target:File = Android.GetManifest()
        Local lines := target.FindLines(searchStr)

        For Local i:Int = 1 To lines.Length()
            Local line := lines[lines.Length() - i]
            target.RemoveLine(line)
        End

        Return lines.Length()
    End

    Function AddPermission:Void(permission:String)
        Local addBefore:String = "<application"
        Local searchStr:String = "uses-permission android:name=~q" + permission + "~q"
        Local patchStr:String = "<" + searchStr + " />"
        Local target:File = Android.GetManifest()

        If target.Contains(searchStr) Then Return

        If Not target.Contains(addBefore)
            app.LogWarning("Unable to add required permission to AndroidManifest.xml")
            app.LogWarning("Please add the following permission manually:")
            app.LogWarning("    " + patchStr)
        Else
            target.InsertBefore(addBefore, patchStr)
        End
    End

    Function EnsureLibsFolder:Void()
        app.TargetDir("libs").Create()
    End
End
