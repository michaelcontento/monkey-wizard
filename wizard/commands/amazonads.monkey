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
        PatchLayout(app)
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

    Method PatchLayout:Void(app:App)
        Local match:String = "android:id=~q@+id/amazonAdsView~q"
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
            "~t~t~tandroid:id=~q@+id/amazonAdsView~q~n" +
            "~t~t~tandroid:layout_width=~qmatch_parent~q~n" +
            "~t~t~tandroid:layout_height=~qwrap_content~q~n" +
            "~t~t~tandroid:layout_alignParentBottom=~qtrue~q >~n" +
            "~t~t</LinearLayout>~n" +
            "~t</RelativeLayout>~n" +
            "</FrameLayout>~n"

        If target.Contains(topSearch) And target.Contains(bottomSearch)
            target.InsertAfter(topSearch, topValue)
            target.InsertAfter(bottomSearch, bottomValue)
        Else
            app.LogWarning("Unable to add AmazonAds layout elements")
            app.LogWarning("A layout with @id/amazonAdsView is required!")
            app.LogWarning("I've tried to add this:~n" +
                topValue + "~n" + "%MONKEY_VIEW%~n" + bottomValue)
        End
    End
End
