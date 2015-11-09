Strict

Private

Import wizard.android
Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file

Public

Class AndroidChartboost Implements Command

    Method Run:Void(app:App)
        Android.AddPermission("android.permission.INTERNET")
        Android.AddPermission("android.permission.ACCESS_NETWORK_STATE")
        Android.AddPermission("android.permission.WRITE_EXTERNAL_STORAGE")
        Android.AddPermission("android.permission.ACCESS_WIFI_STATE")
        Android.AddPermission("android.permission.READ_PHONE_STATE")
        PatchActivity(app)
        CopyLibs(app)
    End

    Method PatchActivity:Void(app:App)
        Local match:String = "</application>"
        Local patchStr:String = "<activity android:name=~qcom.chartboost.sdk.CBImpressionActivity~q android:excludeFromRecents=~qtrue~q android:hardwareAccelerated=~qtrue~q android:theme=~q@android:style/Theme.Translucent.NoTitleBar.Fullscreen~q android:configChanges=~qkeyboardHidden|orientation|screenSize~q /> ~n"
        Local target:File = Android.GetManifest()

        If target.Contains("com.chartboost.sdk.CBImpressionActivity")
            Return
        End

        If Not target.Contains(match)
            app.LogWarning("Unable to add required activity to AndroidManifest.xml")
            app.LogWarning("Please add the following activity manually:")
            app.LogWarning("    " + patchStr)
        Else
            target.InsertBefore(match, patchStr)
        End
    End

    Private

    Method CopyLibs:Void(app:App)
        Android.EnsureLibsFolder()

        Local src:File = app.SourceFile("chartboost.jar")
        Local dst:File = app.TargetFile("libs/chartboost.jar")

        src.CopyTo(dst)
    End
End
