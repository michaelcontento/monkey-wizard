Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios

Public

Class IosLaunchImage Implements Command
    Private

    Const IPAD_LANDSCAPE := "IPAD-LANDSCAPE"
    Const IPAD_PORTAIT := "IPAD-PORTAIT"
    Const IPHONE := "IPHONE"

    Method Run:Void(app:App)
        Select GetMode(app)
        Case IPHONE
            ProcessIPhone(app)
        Case IPAD_LANDSCAPE
            ProcessIPadLandscape(app)
        Case IPAD_PORTAIT
            ProcessIPadPortrait(app)
        Default
            app.LogError("Invalid mode given.")
        End
    End

    Private

    Function GetMode:String(app:App)
        If app.GetAdditionArguments().Length() < 1
            app.LogError("Not enough arguments.")
        End

        Return app.GetAdditionArguments()[0].ToUpper()
    End

    Function ProcessIPhone:Void(app:App)
        CopyImage(app, 4, "Default-568h@2x.png")
        CopyImage(app, 3, "Default@2x.png")
        CopyImage(app, 2, "Default.png")

        AddImage(app, "Default-568h@2x.png")
        AddImage(app, "Default@2x.png")
        AddImage(app, "Default.png")
    End

    Function ProcessIPadLandscape:Void(app:App)
        CopyImage(app, 3, "Default-Landscape@2x~~ipad.png")
        CopyImage(app, 2, "Default-Landscape~~ipad.png")

        AddImage(app, "Default-Landscape@2x~~ipad.png")
        AddImage(app, "Default-Landscape~~ipad.png")
    End

    Function ProcessIPadPortrait:Void(app:App)
        CopyImage(app, 3, "Default-Portrait@2x~~ipad.png")
        CopyImage(app, 2, "Default-Portrait~~ipad.png")

        AddImage(app, "Default-Portrait@2x~~ipad.png")
        AddImage(app, "Default-Portrait~~ipad.png")
    End

    Function CopyImage:Void(app:App, idx:Int, name:String)
        Local args := app.GetAdditionArguments()
        If args.Length() < idx
            app.LogError("Argument " + idx + " for file " + name + " missing")
        End

        Local filename := app.GetAdditionArguments()[idx - 1]
        If Not filename.EndsWith(".png")
            app.LogError("Image must be in PNG format")
        End

        Local dstDir := app.TargetDir("").GetPath()
        Local file := New File(filename)
        file.CopyTo(dstDir + name)
    End

    Function AddImage:Void(app:App, filename:String)
        Local firstId := Ios.GenerateUniqueId()
        Local secondId := Ios.GenerateUniqueId()

        Ios.AddIconPBXBuildFile(filename, firstId, secondId)
        Ios.AddIconPBXFileReference(filename, secondId)
        Ios.AddIconPBXGroup(filename, secondId)
        Ios.AddIconPBXResourcesBuildPhase(filename, firstId)
    End
End
