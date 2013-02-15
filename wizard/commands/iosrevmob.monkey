Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios

Public

Class IosRevmob Implements Command
    Method Run:Void(app:App)
        Ios.AddFramework(app, "AdSupport.framework", True)
        Ios.AddFramework(app, "StoreKit.framework")
        Ios.AddFramework(app, "SystemConfiguration.framework")

        Ios.AddFrameworkFromPath(app, "RevMobAds.framework")
        CopyFramework(app)

        app.LogInfo("Monkey interface can be found here: http://goo.gl/yVTiV")
    End

    Private

    Method CopyFramework:Void(app:App)
        Local src:Dir = app.SourceDir("RevMobAds.framework")
        Local dst:Dir = app.TargetDir("RevMobAds.framework")
        src.CopyTo(dst)

        ' The framework contains some symlinks and they are resolved within the
        ' copy step (see: monkey.os.CopyDir) and we can remove the symlink
        ' target directory to get things small and cleaned up
        Local target:Dir = app.TargetDir("RevMobAds.framework/Versions")
        If target.Exists() Then target.Remove()
    End
End
