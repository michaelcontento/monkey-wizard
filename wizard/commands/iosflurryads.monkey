Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios

Public

Class IosFlurryAds Implements Command
    Const VERSION:String = "4.1.0"

    Method Run:Void(app:App)
        Ios.AddFrameworkSystemConfiguration(app)

        Ios.AddFrameworkFromPath(app,
            "FlurryAds.framework",
            "C881932B1682981E00B054F9",
            "C881932A1682981E00B054F9")
        CopyFramework(app)

        app.LogInfo("!! You need to add IosFlurry too !!")
    End

    Private

    Method CopyFramework:Void(app:App)
        Local src:Dir = app.SourceDir("FlurryAds")
        Local dst:Dir = app.TargetDir("FlurryAds.framework")

        If dst.Exists() Then dst.Remove()
        src.CopyTo(dst)

        ConvertToFrameworkDir(app)
    End

    Method ConvertToFrameworkDir:Void(app:App)
        Local fw:String = "FlurryAds.framework"

        app.TargetDir(fw + "/Headers").Create()
        app.TargetDir(fw + "/Resources").Create()

        Local header:File = app.TargetFile(fw + "/FlurryAds.h")
        header.CopyTo(app.TargetFile(fw + "/Headers/FlurryAds.h"))
        header.Remove()

        Local header2:File = app.TargetFile(fw + "/FlurryAdDelegate.h")
        header2.CopyTo(app.TargetFile(fw + "/Headers/FlurryAdDelegate.h"))
        header2.Remove()

        Local lib:File = app.TargetFile(fw + "/libFlurryAds.a")
        lib.CopyTo(app.TargetFile(fw + "/FlurryAds"))
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
            "~t<string>FlurryAds</string>" +
            "~t<key>CFBundleIdentifier</key>" +
            "~t<string>com.flurry.FlurryAds</string>" +
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
