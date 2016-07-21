Strict

Private

Import wizard.app
Import wizard.command
Import wizard.android

Public

Class AndroidSetTargetSdk Implements Command
    Method Run:Void(app:App)
        Local minSdk := "11"
        Local target := "23"
        Local manifest := Android.GetManifest()

        Local sdkLines := manifest.FindLines("uses-sdk")

        If sdkLines.Length() <> 1
            app.LogError("Found zero or more than one 'uses-sdk' property")
        End

        Local line := "<uses-sdk android:minSdkVersion=~q" + minSdk +"~q android:targetSdkVersion=~q" + target + "~q />"
        manifest.ReplaceLine(sdkLines[0], line)
    End
End
