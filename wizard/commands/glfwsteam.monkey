Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios
Import os
Import wizard.cocoapods

Public

Class GlfwSteam Implements Command
    Method Run:Void(app:App)
        Local dir := app.SourceDir("steamsdk")
        dir.CopyTo(app.TargetDir("").GetPath() + "/steamsdk")

        Local libSrc := app.TargetFile("steamsdk/redistributable_bin/osx32/libsteam_api.dylib")
        Local libDest := app.TargetFile("xcode/libsteam_api.dylib")
        libSrc.CopyTo(libDest)
       
        Ios.defaultProjectFile = "xcode/MonkeyGame.xcodeproj/project.pbxproj"
        Local fileId$ = Ios.AddDylib("libsteam_api.dylib")

        Local steamFileId$ = Ios.AddTextFile("data/steam_appid.txt")

        If (steamFileId <> "")
            Ios.AddCopyBuildPhase([["libsteam_api.dylib", fileId], ["data/steam_appid.txt", steamFileId]])
        Else
            Ios.AddCopyBuildPhase([["libsteam_api.dylib", fileId]])
        End

        ' Make copy
        Local file := app.TargetFile("xcode/MonkeyGame.xcodeproj/project.pbxproj")
        Local dest := app.TargetFile("xcode/MonkeyGame.xcodeproj/project.pbxproj.original")
        file.CopyTo(dest)        

        app.LogInfo("Steam SDK installed!")
    End
   
End
