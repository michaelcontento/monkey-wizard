Strict

Private

Import wizard.android
Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file

Public

Class AmazonPayment Implements Command
    Const VERSION:String = "1.0.3"

    Method Run:Void(app:App)
        Android.AddPermission("android.permission.INTERNET")
        PatchReceiver(app)
        CopyLibs(app)
        PrintDeveloperHints(app)
    End

    Private

    Method PatchReceiver:Void(app:App)
        Local match:String = "</application>"
        Local patchStr:String = "" +
            "<receiver android:name=~qcom.amazon.inapp.purchasing.ResponseReceiver~q >~n" +
            "~t<intent-filter>~n" +
            "~t<action android:name=~qcom.amazon.inapp.purchasing.NOTIFY~q~n" +
            "~t~tandroid:permission=~qcom.amazon.inapp.purchasing.Permission.NOTIFY~q />~n" +
            "~t</intent-filter>" +
            "</receiver>"
        Local target:File = Android.GetManifest()

        If target.Contains("com.amazon.inapp.purchasing.ResponseReceiver")
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

        Local src:File = app.SourceFile("in-app-purchasing-" + VERSION + ".jar")
        Local dst:File = app.TargetFile("libs/in-app-purchasing-" + VERSION + ".jar")

        src.CopyTo(dst)
    End

    Method PrintDeveloperHints:Void(app:App)
        Local tester:File = app.SourceFile("AmazonSDKTester.apk")
        Local json:File = app.SourceFile("amazon.sdktester.json")

        app.LogInfo("Monkey interface can be found here: http://goo.gl/JGfIm")
        app.LogInfo("")
        app.LogInfo("And if you want to test the IAP process please note this files:")
        app.LogInfo("1) Amazon SDK Tester App")
        app.LogInfo("   => " + tester.GetPath())
        app.LogInfo("2) Example SDKTester JSON file")
        app.LogInfo("   => " + json.GetPath())
        app.LogInfo("")
        app.LogInfo("Unsure how or where to push the json file?")
        app.LogInfo("   => adb push amazon.sdktester.json /mnt/sdcard/amazon.sdktester.json")
    End
End
