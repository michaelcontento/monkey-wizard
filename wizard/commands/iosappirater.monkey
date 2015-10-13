Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.ios
Import wizard.cocoapods
Public

Class IosAppirater Implements Command
    Method Run:Void(app:App)
        CocoaPods.Init(app)        
        CocoaPods.AddDependency("Appirater", app)
        CocoaPods.Install(app)   
        AddRegionFiles(app)
    End

    Private

    Function AddRegionFiles:Void(app:App)
        Local regions := GetRegions()
        Local ids := New String[regions.Length()]

        For Local i := 0 Until regions.Length()
            Local id := Ios.GenerateUniqueId()
            ids[i] = id

            Ios.AddPbxFileReferenceLProj(
                ids[i],
                regions[i],
                "AppiraterLocalizable.strings")
            Ios.AddKnownRegion(regions[i])
        End

        Local variantId := Ios.GenerateUniqueId()
        Ios.AddPBXVariantGroup(
            variantId,
            "AppiraterLocalizable.strings",
            ids,
            regions)

        Local groupId := Ios.GenerateUniqueId()
        Ios.RegisterPxbGroup("appirater", groupId)
        Ios.AddPbxGroup("appirater", groupId, "appirater")
        Ios.AddPbxGroupChild("appirater", "AppiraterLocalizable.strings", variantId)

        Local fileId := Ios.GenerateUniqueId()
        Ios.AddPbxBuildFile("AppiraterLocalizable.strings", fileId, variantId, False)
        Ios.AddPbxResource("AppiraterLocalizable.strings", fileId)
    End

    Function GetRegions:String[]()
        Return [
            "ca", "cs", "da", "de", "el", "en", "es",
            "fi", "fr", "he", "hu", "it", "ja", "ko",
            "nb", "nl", "pl", "pt", "ru", "sk", "sv",
            "tr", "zh-Hans", "zh-Hant"]
    End
End
