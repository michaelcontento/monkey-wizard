Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios

Public

Class IosAddLanguage Implements Command
    Private


    Method Run:Void(app:App)
        Local lang := GetLang(app)
        Local langPath := lang + ".lproj"      
        Local name := "localize.strings"

        Local firstId := Ios.GenerateUniqueId()
        Local secondId := Ios.GenerateUniqueId()
        Local thirdId := Ios.GenerateUniqueId()

        Local dir := New Dir(app.TargetDir(langPath).GetPath())
        dir.Create()

        Local file := New File(app.TargetDir("").GetPath() + langPath + "/" + name)
        file.data = "/* Localize Strings */"
        file.Save()

        Local buildsection := Ios.GetProject().GetContentBetween("Begin PBXBuildFile section", "End PBXBuildFile section")

        Ios.AddPbxBuildFile(langPath + "/" + name, firstId, secondId, false)
        Ios.AddPbxFileReferenceFile(thirdId, lang, langPath + "/" + name, "file")
        Ios.AddIconPBXGroup(langPath + "/" + name, secondId)
        Ios.AddIconPBXResourcesBuildPhase(langPath + "/" + name, firstId)
        
        Ios.AddPBXVariantGroup(secondId, name, [thirdId], [lang])
    End
    
    Private

    Function GetLang:String(app:App)
        If app.GetAdditionArguments().Length() < 1
            app.LogError("Not enough arguments. Language code required.")
        End

        Return app.GetAdditionArguments()[0]
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

        If Not file.Exists()
            app.LogError("Invalid file given: " + file.GetPath())
        End
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
