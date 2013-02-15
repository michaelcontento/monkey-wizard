Strict

Private

Import wizard.android
Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file

Public

Class GooglePayment Implements Command
    Method Run:Void(app:App)
        Android.AddPermission("android.permission.INTERNET")
        Android.AddPermission("com.android.vending.BILLING")
        PatchServiceAndReceiver(app)

        CopyAndroidBillingLibrary(app)
        PatchAndroidBillingLibray(app)
        PatchBuildXml(app)

        app.LogInfo("Monkey interface can be found here: http://goo.gl/QoYEC")
    End

    Private

    Method PatchServiceAndReceiver:Void(app:App)
        Local match:String = "</application>"
        Local patchStr:String = "" +
            "<service android:name=~qnet.robotmedia.billing.BillingService~q />~n" +
            "<receiver android:name=~qnet.robotmedia.billing.BillingReceiver~q>~n" +
            "~t<intent-filter>~n" +
            "~t~t<action android:name=~qcom.android.vending.billing.IN_APP_NOTIFY~q />~n" +
            "~t~t<action android:name=~qcom.android.vending.billing.RESPONSE_CODE~q />~n" +
            "~t~t<action android:name=~qcom.android.vending.billing.PURCHASE_STATE_CHANGED~q />~n" +
            "~t</intent-filter>~n" +
            "</receiver>"
        Local target:File = Android.GetManifest()

        If target.Contains("net.robotmedia.billing.BillingService")
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

    Method PatchBuildXml:Void(app:App)
        Local patchStr:String = "<copy todir=~qsrc~q><fileset dir=~qsrc_AndroidBillingLibrary~q/></copy>"
        Local match:String = "</project>"
        Local target:File = app.TargetFile("build.xml")

        If Not target.Contains(patchStr)
            If Not target.Contains(match)
                app.LogWarning("Unable to add required copy instructions to build.xml")
                app.LogWarning("please add the following line to your build.xml:")
                app.LogWarning("    " + patchStr)
            Else
                target.InsertBefore(match, patchStr)
            End
        End
    End

    Method CopyAndroidBillingLibrary:Void(app:App)
        Local src:Dir = app.SourceDir("AndroidBillingLibrary/AndroidBillingLibrary/src")
        Local dst:Dir = app.TargetDir("src_AndroidBillingLibrary/src")

        If Not src.Exists() Then app.LogError(
            "AndroidBillingLibrary folger seems to be empty! " +
            "Did you run 'git submodule update --init'?")
        If Not dst.Parent().Exists() Then dst.Parent().Create()
        src.CopyTo(dst)
    End

    Method PatchAndroidBillingLibray:Void(app:App)
        Local target:File = app.TargetFile("src_AndroidBillingLibrary/net/robotmedia/billing/BillingRequest.java")
        Local match:String = "if (response == ResponseCode.RESULT_OK) {"

        Local alreadyPatchedMarker:String = "// MONKEY-WIZARD-PATCHED"
        If target.Contains(alreadyPatchedMarker) Then Return

        If Not target.Exists() Or
           Not target.GetLine(220).Contains(match) Or
           Not target.GetLine(222).Contains("}")
            app.LogWarning("Unable to patch the AndroidBillingLibrary")
            app.LogWarning("Please remove this if statement:")
            app.LogWarning("    " + match)
            app.LogWarning("Around line 220 (including the closing }) in:")
            app.LogWarning("    " + target.GetPath())
            app.LogWarning("But only the condition and _NOT_ the enclosed statement!")
        Else
            target.RemoveLine(222)
            target.RemoveLine(220)
            target.Append("~n" + alreadyPatchedMarker)
        End
    End
End
