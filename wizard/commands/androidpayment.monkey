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
        Local match:String = "</project>"
        Local target:File = app.TargetFile("build.xml")

        If Not target.Contains(patchStr)
            If Not target.Contains(match)
                app.LogWarning("Unable to add required copy instructions to build.xml")
                app.LogWarning("please add the following line to your build.xml:")
                app.LogWarning("    " + match)
            Else
                target.InsertBefore(match, patchStr)
            End
        End
    End

    Method PatchAndroidManifestPermissions:Void(app:App)
        Local patchStr:String = "<uses-permission android:name=~qcom.android.vending.BILLING~q />"
        Local match:String = "<uses-permission android:name=~qandroid.permission.INTERNET~q></uses-permission>"
        Local target:File = app.TargetFile("AndroidManifest.xml")

        If Not target.Contains(patchStr)
            If Not target.Contains(match)
                app.LogWarning("Unable to add required permission to AndroidManifest.xml")
                app.LogWarning("Please add the following permission manually:")
                app.LogWarning("    " + match)
            Else
                target.InsertBefore(match, patchStr)
            End
        End
    End
End
