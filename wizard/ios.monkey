Strict

Private

Import wizard.app
Import wizard.file

Public

Class Ios Abstract
    Function AddFrameworkAdSupport:Void(app:App)
        AddFramework(app,
            "AdSupport.framework",
            "C82D748416827CC600AFC5FC",
            "C82D748316827CC600AFC5FC")
    End

    Function AddFrameworkSystemConfiguration:Void(app:App)
        AddFramework(app,
            "SystemConfiguration.framework",
            "C836AAD516827E7400CA5862",
            "C836AAD416827E7400CA5862")
    End

    Function AddFrameworkStoreKit:Void(app:App)
        AddFramework(app,
            "StoreKit.framework",
            "C836AAD216827E6F00CA5862",
            "C836AAD116827E6F00CA5862")
    End

    Function AddFramework:Void(app:App, name:String, firstId:String, secondId:String)
        AddPbxFileReferenceSdk(app, name, secondId)

        AddPbxBuildFile(app, name, firstId, secondId)
        AddPbxFrameworkBuildPhase(app, name, firstId)
        AddPbxGroup(app, name, secondId)
    End

    Function AddFrameworkFromPath:Void(app:App, name:String, firstId:String, secondId:String)
        AddPbxFileReferencePath(app, name, secondId)
        EnsureSearchPathWithSRCROOT(app, "Debug")
        EnsureSearchPathWithSRCROOT(app, "Release")

        AddPbxBuildFile(app, name, firstId, secondId)
        AddPbxFrameworkBuildPhase(app, name, firstId)
        AddPbxGroup(app, name, secondId)
    End

    Function GetProject:File(app:App)
        Return app.TargetFile("MonkeyGame.xcodeproj/project.pbxproj")
    End

    Private

    Function EnsureSearchPathWithSRCROOT:Void(app:App, config:String)
        Local searchStr:String = "FRAMEWORK_SEARCH_PATHS"
        Local searchBegin:String = "" +
            "/* " + config + " */ = {~n" +
            "~t~t~tisa = XCBuildConfiguration;"
        Local searchEnd:String = "name = " + config + ";"
        Local target:File = GetProject(app)

        ' Whole setting is missing
        If Not target.ContainsBetween(searchStr, searchBegin, searchEnd)
            Local addAfter:String = "buildSettings = {"
            Local patchStr:String = "" +
                "~t~t~t~tFRAMEWORK_SEARCH_PATHS = (~n" +
                "~t~t~t~t~t~q$(inherited)~q,~n" +
                "~t~t~t~t~t~q\~q$(SRCROOT)\~q~q,~n" +
                "~t~t~t~t);"
            target.InsertAfterBetween(addAfter, patchStr, searchBegin, searchEnd)
            Return
        End

        ' We need just to update the existing setting
        If Not target.ContainsBetween("$(SRCROOT)", searchBegin, searchEnd)
            Local addAfter:String = "FRAMEWORK_SEARCH_PATHS = ("
            Local patchStr:String = "~t~t~t~t~t~q\~q$(SRCROOT)\~q~q,"
            target.InsertAfterBetween(addAfter, patchStr, searchBegin, searchEnd)
        End
    End

    Function AddPbxGroup:Void(app:App, name:String, id:String)
        Local patchStr:String = "~t~t~t~t" + id + " /* " + name + " in Frameworks */,"
        Local searchBegin:String = "29B97323FDCFA39411CA2CEA /* Frameworks */ = {"
        Local searchEnd:String = "End PBXGroup section"
        Local addAfter:String = "children = ("
        Local target:File = GetProject(app)

        If target.ContainsBetween(patchStr, searchBegin, searchEnd) Then Return

        If Not target.ContainsBetween(addAfter, searchBegin, searchEnd)
            app.LogWarning("Unable To add " + name + " PBXGroup")
            app.LogWarning("Please add this framework manually!")
        Else
            target.InsertAfterBetween(addAfter, patchStr, searchBegin, searchEnd)
        End
    End

    Function AddPbxFrameworkBuildPhase:Void(app:App, name:String, id:String)
        Local patchStr:String = "~t~t~t~t" + id + " /* " + name + " in Frameworks */,"
        Local searchBegin:String = "Begin PBXFrameworksBuildPhase section"
        Local searchEnd:String = "End PBXFrameworksBuildPhase section"
        Local addAfter:String = "files = ("
        Local target:File = GetProject(app)

        If target.ContainsBetween(patchStr, searchBegin, searchEnd) Then Return

        If Not target.ContainsBetween(addAfter, searchBegin, searchEnd)
            app.LogWarning("Unable To add " + name + " PBXFrameworksBuildPhase")
            app.LogWarning("Please add this framework manually!")
        Else
            target.InsertAfterBetween(addAfter, patchStr, searchBegin, searchEnd)
        End
    End

    Function AddPbxFileReferenceSdk:Void(app:App, name:String, id:String)
        Local patchStr:String = "~t~t" +
            id + " " +
            "/* " + name + "*/ = " +
            "{isa = PBXFileReference; " +
            "lastKnownFileType = wrapper.framework; " +
            "name = " + name + "; " +
            "path = System/Library/Frameworks/" + name + "; " +
            "sourceTree = SDKROOT; };"

        AddPbxFileReference(app, name, patchStr)
    End

    Function AddPbxFileReferencePath:Void(app:App, name:String, id:String)
        Local patchStr:String = "~t~t" +
            id + " " +
            "/* " + name + " */ = " +
            "{isa = PBXFileReference; " +
            "lastKnownFileType = wrapper.framework; " +
            "path = " + name + "; sourceTree = ~q<group>~q; };"

        AddPbxFileReference(app, name, patchStr)
    End

    Function AddPbxFileReference:Void(app:App, name:String, patchStr:String)
        Local match:String = "/* End PBXFileReference section"
        Local target:File = GetProject(app)

        If target.Contains(patchStr) Then Return

        If Not target.Contains(match)
            app.LogWarning("Unable to add " + name + " PBXFileReference")
            app.LogWarning("Please add this framework manually!")
        Else
            target.InsertBefore(match, patchStr)
        End
    End

    Function AddPbxBuildFile:Void(app:App, name:String, firstId:String, secondId:string)
        Local match:String = "/* End PBXBuildFile section"
        Local patchStr:String = "~t~t" +
            firstId + " " +
            "/* " + name + " in Frameworks */ = " +
            "{isa = PBXBuildFile; " +
            "fileRef = " + secondId + " /* " + name + "*/; };"
        Local target:File = GetProject(app)

        If target.Contains(patchStr) Then Return

        If Not target.Contains(match)
            app.LogWarning("Unable to add " + name + " PBXBuildFile")
            app.LogWarning("Please add this framework manually!")
        Else
            target.InsertBefore(match, patchStr)
        End
    End
End
