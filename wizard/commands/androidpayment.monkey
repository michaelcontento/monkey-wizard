Strict

Private

Import wizard.android
Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file

Public

Class AndroidPayment Implements Command
    Method Run:Void(app:App)
        PatchBuildXml(app)
        Android.AddPermission(app, "android.permission.INTERNET")
        CopyAndroidBillingLibrary(app)
        PatchAndroidBillingLibray(app)
        app.LogInfo("Monkey interface can be found here: http://goo.gl/QoYEC")
    End

    Private

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
        Local target:File = app.TargetFile("src_AndroidBillingLibrary/src/net/robotmedia/billing/BillingRequest.java")
        Local match:String = "if (response == ResponseCode.RESULT_OK) {"

        If Not target.Exists() Or
           Not target.GetLine(220).Contains(match) Or
           Not target.GetLine(222).Contains("}")
            app.LogWarning("Unable to patch the AndroidBillingLibrary")
            app.LogWarning("Please remove this if statement:")
            app.LogWarning("    " + match)
            app.LogWarning("Around line 220 (including the closing }) in:")
            app.LogWarning("    " + target.GetPath())
        Else
            target.RemoveLine(222)
            target.RemoveLine(220)
        End
    End
End
