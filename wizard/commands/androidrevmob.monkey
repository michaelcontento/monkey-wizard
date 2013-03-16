Strict

Private

Import wizard.android
Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file

Public

Class AndroidRevmob Implements Command
    Const VERSION:String = "5.2.3"

    Method Run:Void(app:App)
        PatchActivity(app)
        PatchPermissions()
        CopyLibs(app)
        PatchLayout(app)
        app.LogInfo("Monkey interface can be found here: http://goo.gl/yVTiV")
    End

    Private

    Method PatchActivity:Void(app:App)
        Local match:String = "</application>"
        Local patchStr:String = "<activity~n" +
            "~tandroid:name=~qcom.revmob.ads.fullscreen.FullscreenActivity~q~n" +
            "~tandroid:configChanges=~qkeyboardHidden|orientation~q >~n" +
            "</activity>"
        Local target:File = Android.GetManifest()

        If target.Contains("com.revmob.ads.fullscreen.FullscreenActivity")
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

    Method PatchPermissions:Void()
        Android.AddPermission("android.permission.ACCESS_NETWORK_STATE")
        Android.AddPermission("android.permission.ACCESS_WIFI_STATE")
        Android.AddPermission("android.permission.INTERNET")
        Android.AddPermission("android.permission.READ_PHONE_STATE")
    End

    Method CopyLibs:Void(app:App)
        Android.EnsureLibsFolder()

        Local src:File = app.SourceFile("revmob-" + VERSION + ".jar")
        Local dst:File = app.TargetFile("libs/revmob-" + VERSION + ".jar")

        src.CopyTo(dst)
    End

    Method PatchLayout:Void(app:App)
        Local match:String = "android:id=~q@+id/banner~q"
        Local target:File = app.TargetFile("templates/res/layout/main.xml")

        If target.Contains(match) Then Return


        '
        ' Find monkey view
        '
        Local monkeyView:String = target.GetContentBetween("<view class=", "/>")

        If (monkeyView <> "")
            Local manifestTemplate:File = app.SourceFile("manifestTemplate.xml")
            manifestTemplate.Replace("{%%MONKEY_VIEW%%}", monkeyView)

            target.data = manifestTemplate.Get()
        Else
            app.LogWarning("Unable to add revmob layout elements")
            app.LogWarning("A layout with @id/banner is required!")
            app.LogWarning("Monkey view wasn't found in base-manifest")
        End
    End
End
