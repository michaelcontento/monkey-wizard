Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.helperandroid

Public

Class AndroidRevmob Implements Command
    Const VERSION:String = "5.1.1"

    Method Run:Void(app:App)
        PatchActivity(app)
        PatchPermissions(app)
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
        Local target:File = Android.GetManifest(app)

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

    Method PatchPermissions:Void(app:App)
        Android.AddPermission(app, "android.permission.ACCESS_NETWORK_STATE")
        Android.AddPermission(app, "android.permission.ACCESS_WIFI_STATE")
        Android.AddPermission(app, "android.permission.INTERNET")
        Android.AddPermission(app, "android.permission.READ_PHONE_STATE")
    End

    Method CopyLibs:Void(app:App)
        Android.EnsureLibsFolder(app)

        Local src:File = app.SourceFile("revmob-" + VERSION + ".jar")
        Local dst:File = app.TargetFile("libs/revmob-" + VERSION + ".jar")

        src.CopyTo(dst)
    End

    Method PatchLayout:Void(app:App)
        Local match:String = "android:id=~q@+id/banner~q"
        Local target:File = app.TargetFile("templates/res/layout/main.xml")

        If target.Contains(match) Then Return

        Local topSearch:String = "<?xml version=~q1.0~q encoding=~qutf-8~q?>"
        Local topValue:String = "" +
            "<FrameLayout~n" +
            "~tandroid:id=~q@+id/mainframe~q~n" +
            "~tandroid:layout_width=~qfill_parent~q~n" +
            "~txmlns:android=~qhttp://schemas.android.com/apk/res/android~q~n" +
            "~tandroid:layout_height=~qfill_parent~q >"

        Local bottomSearch:String = "</LinearLayout>"
        Local bottomValue:String = "" +
            "~t<RelativeLayout~n" +
            "~t~t~tandroid:layout_width=~qfill_parent~q~n" +
            "~t~t~tandroid:layout_height=~qfill_parent~q >~n" +
            "~t~t<LinearLayout~n" +
            "~t~t~tandroid:id=~q@+id/banner~q~n" +
            "~t~t~tandroid:layout_width=~qwrap_content~q~n" +
            "~t~t~tandroid:layout_height=~qwrap_content~q~n" +
            "~t~t~tandroid:layout_alignParentBottom=~qtrue~q >~n" +
            "~t~t</LinearLayout>~n" +
            "~t</RelativeLayout>~n" +
            "</FrameLayout>~n"

        If target.Contains(topSearch) And target.Contains(bottomSearch)
            target.InsertAfter(topSearch, topValue)
            target.InsertAfter(bottomSearch, bottomValue)
        Else
            app.LogWarning("Unable to add revmob layout elements")
            app.LogWarning("A layout with @id/banner is required!")
            app.LogWarning("I've tried to add this:~n" +
                topValue + "~n" + "%MONKEY_VIEW%~n" + bottomValue)
        End
    End
End