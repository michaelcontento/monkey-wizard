Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios

Public

Class IosFlurry Implements Command
    Const VERSION:String = "4.1.0"

    Method Run:Void(app:App)
        Ios.AddFramework("SystemConfiguration.framework")

        Ios.AddFrameworkFromPath("Flurry.framework")
        CopyFramework(app)

        app.LogInfo("Monkey interface can be found here: http://goo.gl/d7jh5")
    End

    Private

    Method CopyFramework:Void(app:App)
        Local src:Dir = app.SourceDir("Flurry")
        Local dst:Dir = app.TargetDir("Flurry.framework")

        If dst.Exists() Then dst.Remove()
        src.CopyTo(dst)

        ConvertToFrameworkDir(app)
    End

    Method ConvertToFrameworkDir:Void(app:App)
        Local fw:String = "Flurry.framework"

        app.TargetDir(fw + "/Headers").Create()
        app.TargetDir(fw + "/Resources").Create()

        Local header:File = app.TargetFile(fw + "/Flurry.h")
        header.CopyTo(app.TargetFile(fw + "/Headers/Flurry.h"))
        header.Remove()

        Local lib:File = app.TargetFile(fw + "/libFlurry.a")
        lib.CopyTo(app.TargetFile(fw + "/Flurry"))
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
            "~t<string>Flurry</string>" +
            "~t<key>CFBundleIdentifier</key>" +
            "~t<string>com.flurry.Flurry</string>" +
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
