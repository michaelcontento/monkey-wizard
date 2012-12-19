Strict

Private

Import wizard.app
Import wizard.file
Import wizard.command

Public

Class AndroidPayment Implements Command
    Method Run:Void(app:App)
        PatchBuildXml(app)
        PatchAndroidManifestPermissions(app)
    End

    Private

    Method PatchBuildXml:Void(app:App)
        Local patchStr:String = "<copy todir=~qsrc~q><fileset dir=~qsrc_AndroidBillingLibrary~q/></copy>"
        Local target:File = app.TargetFile("build.xml")

        If Not target.Contains(patchStr)
            target.InsertBefore("</project>", patchStr)
        End
    End

    Method PatchAndroidManifestPermissions:Void(app:App)
        Local patchStr:String = "<uses-permission android:name=~qcom.android.vending.BILLING~q />"
        Local target:File = app.TargetFile("AndroidManifest.xml")

        If Not target.Contains(patchStr)
            Local match:String = "<uses-permission android:name=~qandroid.permission.INTERNET~q></uses-permission>"
            target.InsertBefore(match, patchStr)
        End
    End
End
