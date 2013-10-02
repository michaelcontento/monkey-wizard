Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios

Public

Class IosChartboost Implements Command
    Const VERSION:String = "3.2"

    Method Run:Void(app:App)
        Ios.AddFramework("AdSupport.framework", True)
        Ios.AddFramework("StoreKit.framework")
        Ios.AddFramework("SystemConfiguration.framework")
        Ios.AddFramework("QuartzCore.framework")
        Ios.AddFramework("GameKit.framework")

        Ios.AddFrameworkFromPath("Chartboost.framework")
        CopyFramework(app)
    End

    Private

    Method CopyFramework:Void(app:App)
        Local src:Dir = app.SourceDir("Chartboost-" + VERSION)
        Local dst:Dir = app.TargetDir("Chartboost.framework")

        If dst.Exists() Then dst.Remove()
        src.CopyTo(dst)

        ConvertToFrameworkDir(app)
    End

    Method ConvertToFrameworkDir:Void(app:App)
        Local fw:String = "Chartboost.framework"

        app.TargetDir(fw + "/Headers").Create()
        app.TargetDir(fw + "/Resources").Create()

        Local header:File = app.TargetFile(fw + "/Chartboost.h")
        header.CopyTo(app.TargetFile(fw + "/Headers/Chartboost.h"))
        header.Remove()

        Local lib:File = app.TargetFile(fw + "/libChartboost.a")
        lib.CopyTo(app.TargetFile(fw + "/Chartboost"))
        lib.Remove()

        Local plist:File = app.TargetFile(fw + "/Resources/Info.plist")
        plist.Set("" +
            "<?xml version=~q1.0~q encoding=~qUTF-8~q?>" +
            "<!DOCTYPE plist PUBLIC ~q-//Apple//DTD PLIST 1.0//EN~q ~qhttp://www.apple.com/DTDs/PropertyList-1.0.dtd~q>" +
            "<plist version=~q1.0~q>" +
            "<dict>" +
            "~t<key>CFBuildHash</key>" +
            "~t<string></string>" +
            "~t<key>CFBundleDevelopmentRegion</key>" +
            "~t<string>English</string>" +
            "~t<key>CFBundleExecutable</key>" +
            "~t<string>Chartboost</string>" +
            "~t<key>CFBundleIdentifier</key>" +
            "~t<string>com.chartboost.Chartboost</string>" +
            "~t<key>CFBundleInfoDictionaryVersion</key>" +
            "~t<string>6.0</string>" +
            "~t<key>CFBundlePackageType</key>" +
            "~t<string>FMWK</string>" +
            "~t<key>CFBundleShortVersionString</key>" +
            "~t<string>" + VERSION + "</string>" +
            "~t<key>CFBundleSignature</key>" +
            "~t<string>?</string>" +
            "~t<key>CFBundleVersion</key>" +
            "~t<string>" + VERSION + "</string>" +
            "</dict>" +
            "</plist>")
    End
End
