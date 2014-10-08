Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios

Public

Class IosChartboost Implements Command
    Method Run:Void(app:App)
        Ios.AddFramework("AdSupport.framework", True)
        Ios.AddFramework("StoreKit.framework")
        Ios.AddFramework("SystemConfiguration.framework")
        Ios.AddFramework("QuartzCore.framework")
        Ios.AddFramework("GameKit.framework")

        CopyFramework(app)
        app.LogInfo("Chartboost installed!")
    End

    Private

    Method CopyFramework:Void(app:App)
        Local src:Dir = app.SourceDir("Chartboost.framework")
        Local dst:Dir = app.TargetDir("Chartboost.framework")
        src.CopyTo(dst)

        ' The framework contains some symlinks and they are resolved within the
        ' copy step (see: monkey.os.CopyDir) and we can remove the symlink
        ' target directory to get things small and cleaned up
        Local target:Dir = app.TargetDir("Chartboost.framework/Versions")
        If target.Exists() Then target.Remove()

        Ios.AddFrameworkFromPath("Chartboost.framework")

        Ios.GetProject().InsertAfter(
            "~t~t~t~tFRAMEWORK_SEARCH_PATHS = (",
            "~t~t~t~t~t~q\~q$(PROJECT_DIR)/Chartboost.framework\~q~q,")        

        'Portrait orientation is required so it doesn't crash!
        'AddOrientationPortrait()
    End
End
