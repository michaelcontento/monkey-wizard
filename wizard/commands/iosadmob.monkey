Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios

Public

Class IosAdmob Implements Command
    Method Run:Void(app:App)
        CopyFramework(app)
        app.LogInfo("Admob added")
    End

    Private

    Method CopyFramework:Void(app:App)
        Local src:Dir = app.SourceDir("GoogleMobileAdsSdkiOS-7.0.0/GoogleMobileAds.framework")
        Local dst:Dir = app.TargetDir("GoogleMobileAds.framework")
        src.CopyTo(dst)

        Ios.AddFrameworkFromPath("GoogleMobileAds.framework")

        Ios.GetProject().InsertAfter(
            "~t~t~t~tFRAMEWORK_SEARCH_PATHS = (",
            "~t~t~t~t~t~q\~q$(PROJECT_DIR)/GoogleMobileAds.framework\~q~q,")   

        ' The framework contains some symlinks and they are resolved within the
        ' copy step (see: monkey.os.CopyDir) and we can remove the symlink
        ' target directory to get things small and cleaned up
        ' Local target:Dir = app.TargetDir("GoogleMobileAds.framework/Versions")
        ' If target.Exists() Then target.Remove()
    End
End
