Strict

Private

Import wizard.android
Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file

Public

Class AndroidLocalytics Implements Command
    Const VERSION := "2.4"

    Method Run:Void(app:App)
        Android.AddPermission("android.permission.INTERNET")

        CopyAndroidBillingLibrary(app)
        PatchBuildXml(app)

        app.LogInfo("Monkey interface can be found here: http://goo.gl/0w5AG")
    End

    Private

    Method PatchBuildXml:Void(app:App)
        Local patchStr:String = "<copy todir=~qsrc~q><fileset dir=~qsrc_Localytics~q/></copy>"
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
        Local src:Dir = app.SourceDir("LocalyticsSession-" + VERSION + "/src")
        Local dst:Dir = app.TargetDir("src_Localytics/src")

        If Not dst.Parent().Exists() Then dst.Parent().Create()
        src.CopyTo(dst)
    End
End
