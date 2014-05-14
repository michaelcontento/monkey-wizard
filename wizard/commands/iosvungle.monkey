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
        CopyFramework(app)

        app.LogInfo("Vungle installed!")
    End

    Private

    Method CopyFramework:Void(app:App)
        Local src:Dir = app.SourceDir("vunglepub.embeddedframework")
        Local dst:Dir = app.TargetDir("vunglepub.embeddedframework")
        src.CopyTo(dst)

        ' The framework contains some symlinks and they are resolved within the
        ' copy step (see: monkey.os.CopyDir) and we can remove the symlink
        ' target directory to get things small and cleaned up
        Local target:Dir = app.TargetDir("vunglepub.embeddedframework/vunglepub.framework/Versions")
        If target.Exists() Then target.Remove()

        AddLibZ()

        Ios.AddFrameworkFromPath("vunglepub.embeddedframework")

        Ios.GetProject().InsertAfter(
            "~t~t~t~tFRAMEWORK_SEARCH_PATHS = (",
            "~t~t~t~t~t~q\~q$(PROJECT_DIR)/vunglepub.embeddedframework\~q~q,")        

        'Portrait orientation is required so it doesn't crash!
        AddOrientationPortrait()
    End

    Method AddOrientationPortrait:Void()
        Local file := Ios.GetPlist()
        file.InsertBefore(
            "</dict>",
            "~t<key>UISupportedInterfaceOrientations</key>~n" +
            "~t<array>~n" +
            "~t~t<string>UIInterfaceOrientationPortrait</string>~n" +
            "~t~t<string>UIInterfaceOrientationPortraitUpsideDown</string>~n" +
            "~t</array>~n" +
            "~t<key>UISupportedInterfaceOrientations~~ipad</key>~n" +
            "~t<array>~n" +
            "~t~t<string>UIInterfaceOrientationPortrait</string>~n" +
            "~t~t<string>UIInterfaceOrientationPortraitUpsideDown</string>~n" +
            "~t</array>")
    End

    Method AddLibZ:Void()
        Local firstId:String = Ios.GenerateUniqueId()
        Local secondId:String = Ios.GenerateUniqueId()

        Local name := "libz.dylib"

        Ios.AddPbxBuildFile(name, firstId, secondId, False)

        Local patchStr:String = "~t~t" +
            secondId + " " +
            "/* " + name + " */ = " +
            "{isa = PBXFileReference; " +
            "lastKnownFileType = ~qcompiled.mach-o.dylib~q; " +
            "name = " + name + "; " +
            "path = usr/lib/libz.dylib; " +
            "sourceTree = SDKROOT; };"
        Ios.AddPbxFileReference(name, patchStr)

        Ios.AddPbxFrameworkBuildPhase(name, firstId)

        Ios.AddPbxGroupChild("Frameworks", name, secondId)
    End
End
