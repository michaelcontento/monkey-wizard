Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.ios

Public

Class IosAppirater Implements Command
    Method Run:Void(app:App)
        Ios.AddFramework("CFNetwork.framework")
        Ios.AddFramework("StoreKit.framework")
        Ios.AddFramework("SystemConfiguration.framework")

        CopyFramework(app)
        AddRegionFiles(app)
        AddSourceFiles(app)
    End

    Private

    Function CopyFramework:Void(app:App)
        Local src:Dir = app.SourceDir("appirater")
        Local dst:Dir = app.TargetDir("appirater")
        src.CopyTo(dst, True, False)
    End

    Function AddSourceFiles:Void(app:App)
        Local files := [
            [Ios.GenerateUniqueId(), "Appirater.m", "sourcecode.c.objc"],
            [Ios.GenerateUniqueId(), "Appirater.h", "sourcecode.c.h"],
            [Ios.GenerateUniqueId(), "AppiraterDelegate.h", "sourcecode.c.h"]]

        For Local row := EachIn files
            Ios.AddPbxFileReferenceFile(row[0], row[1], row[1], row[2])
            Ios.AddPbxGroupChild("appirater", row[1], row[0])
        End

        ' --- Add sources files to buildgroup
        Local fileId := Ios.GenerateUniqueId()
        Ios.AddPbxBuildFile("Appirater.m", fileId, files[0][0], False)
        Ios.GetProject().InsertAfter(
            "/* main.mm in Sources */,",
            "~t~t~t~t" + fileId + " /* Appirater.m in Sources */,")
    End

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
        Ios.AddPbxGroup("appirater", groupId)
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
