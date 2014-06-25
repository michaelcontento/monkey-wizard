Strict

Private

Import wizard.app
Import wizard.command
Import wizard.file
Import wizard.android

Public

Class AndroidTabletSupport Implements Command
    Method Run:Void(app:App)
        Local manifest := Android.GetManifest()

        Local sdkLines := manifest.FindLines("uses-sdk")

        If sdkLines.Length() <> 1
            app.LogError("Found zero or more than one 'uses-sdk' property")
        End

        Local line := "<uses-sdk android:minSdkVersion=~q11~q android:targetSdkVersion=~q14~q />"
        manifest.ReplaceLine(sdkLines[0], line)
    End
End
