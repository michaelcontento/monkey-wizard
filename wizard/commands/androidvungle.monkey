Strict

Private

Import wizard.android
Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file

Public

Class AndroidVungle Implements Command

    Method Run:Void(app:App)
        Android.AddPermission("android.permission.INTERNET")
        Android.AddPermission("android.permission.WRITE_EXTERNAL_STORAGE")
        Android.AddPermission("android.permission.ACCESS_NETWORK_STATE")
        Android.AddPermission("android.permission.ACCESS_FINE_LOCATION")

        CopyLibs(app)

        PatchActivity(app)
    End

    Private

    Method PatchActivity:Void(app:App)
        Local match:String = "</application>"
        Local patchStr:String = "<activity android:name=~com.vungle.publisher.FullScreenAdActivity~q android:configChanges=~keyboardHidden|orientation|screenSize~q android:theme=~q@android:style/Theme.NoTitleBar.Fullscreen~q /> ~n"
        patchStr += "<service android:name=~qcom.vungle.sdk.VungleIntentService~q />"
        Local target:File = Android.GetManifest()

        If target.Contains("com.vungle.publisher.FullScreenAdActivity")
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

        Local src:File = app.SourceFile("vungle-publisher-adaptive-id-3.3.3.jar")
        Local dst:File = app.TargetFile("libs/vungle-publisher-adaptive-id-3.3.3.jar")

        src.CopyTo(dst)

        src = app.SourceFile("support-v4-18.0.0.jar")
        dst = app.TargetFile("libs/support-v4-18.0.0.jar")
        src.CopyTo(dst)

        src = app.SourceFile("nineoldandroids-2.4.0.jar")
        dst = app.TargetFile("libs/nineoldandroids-2.4.0.jar")
        src.CopyTo(dst)

        src = app.SourceFile("javax.inject-1.jar")
        dst = app.TargetFile("libs/javax.inject-1.jar")
        src.CopyTo(dst)

        src = app.SourceFile("dagger-1.2.2.jar")
        dst = app.TargetFile("libs/dagger-1.2.2.jar")
        src.CopyTo(dst)
    End
End
