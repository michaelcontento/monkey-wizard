Strict

Private

Import wizard.android
Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file

Public

Class AmazonAds Implements Command
    Const VERSION:String = "5.1.14"

    Method Run:Void(app:App)
        Android.AddPermission("android.permission.ACCESS_COARSE_LOCATION")
        Android.AddPermission("android.permission.ACCESS_FINE_LOCATION")
        Android.AddPermission("android.permission.ACCESS_NETWORK_STATE")
        Android.AddPermission("android.permission.ACCESS_WIFI_STATE")
        Android.AddPermission("android.permission.INTERNET")
        PatchReceiver(app)
        CopyLibs(app)
    End

    Private

    Method PatchReceiver:Void(app:App)
        Local match:String = "</application>"
        Local patchStr:String = "<activity android:name=~qcom.amazon.device.ads.AdActivity~q android:configChanges=~qkeyboardHidden|orientation|screenSize~q/>"
        Local target:File = Android.GetManifest()

        If target.Contains("com.amazon.device.ads.AdActivity")
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

    Method CopyLibs:Void(app:App)
        Android.EnsureLibsFolder()

        Local src:File = app.SourceFile("amazon-ads-" + VERSION + ".jar")
        Local dst:File = app.TargetFile("libs/amazon-ads-" + VERSION + ".jar")

        src.CopyTo(dst)
    End
End
