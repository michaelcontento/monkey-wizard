Strict

Private

Import wizard.app
Import wizard.command
Import wizard.android

Public

Class AndroidVersion Implements Command
    Method Run:Void(app:App)
        app.LogDeprecated("Use monkeys #ANDROID_VERSION_* settings")

        If app.GetAdditionArguments().Length() <> 1
            app.LogError("First argument must be the new version")
        End

        Local manifest := Android.GetManifest()
        Local codeLines := manifest.FindLines("android:versionCode")
        Local nameLines := manifest.FindLines("android:versionName")

        If codeLines.Length() <> 1
            app.LogError("Found zero or more than one 'android:versionCode' property")
        End
        If nameLines.Length() <> 1
            app.LogError("Found zero or more than one 'android:versionName' property")
        End

        Local codeValue := app.GetAdditionArguments()[0].Replace(".", "")
        Local nameValue := app.GetAdditionArguments()[0]

        manifest.ReplaceLine(codeLines[0], "~tandroid:versionCode=~q" + codeValue + "~q")
        manifest.ReplaceLine(nameLines[0], "~tandroid:versionName=~q" + nameValue + "~q")
    End
End
