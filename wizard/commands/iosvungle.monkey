Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios

Public

Class IosVungle Implements Command
    Method Run:Void(app:App)
        Ios.AddFramework("AdSupport.framework", True)
        Ios.AddFramework("SystemConfiguration.framework")
        Ios.AddFramework("CoreMedia.framework")
        Ios.AddFramework("MediaPlayer.framework")
        Ios.AddFramework("CoreLocation.framework")
        CopyFramework(app)

        app.LogInfo("Vungle installed - Please add libz.dylib and vunglepub.framework manually!!!!")
    End

    Private

    Method CopyFramework:Void(app:App)
        Local src:Dir = app.SourceDir("vunglepub.embeddedframework")
        Local dst:Dir = app.TargetDir("vunglepub.embeddedframework")
        src.CopyTo(dst)

        ' The framework contains some symlinks and they are resolved within the
        ' copy step (see: monkey.os.CopyDir) and we can remove the symlink
        ' target directory to get things small and cleaned up
        ' Local target:Dir = app.TargetDir("vunglepub.embeddedframework/Versions")
        ' If target.Exists() Then target.Remove()
    End
End
