Strict

Private

Import wizard.android
Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file

Public

Class SamsungPayment Implements Command
    Method Run:Void(app:App)
        Android.AddPermission("android.permission.INTERNET")
        Android.AddPermission("com.sec.android.iap.permission.BILLING")
        CopyLibs(app)
        PatchBuildXml(app)
    End

    Private

    Method CopyLibs:Void(app:App)
        Android.EnsureLibsFolder()

        Local src:Dir = app.SourceDir("src")
        Local dst:Dir = app.TargetDir("src_AndroidBillingLibrary/src")

        src.CopyTo(dst)
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
End
