Strict

Private

Import wizard.app
Import wizard.file

Public

Class Ios Abstract
    Function AddFramework:Void(app:App, name:String, optional:Bool=False)
        If ContainsFramework(app, name) Then Return

        Local firstId:String = GenerateUniqueId(app)
        Local secondId:String = GenerateUniqueId(app)

        AddPbxFileReferenceSdk(app, name, secondId)

        AddPbxBuildFile(app, name, firstId, secondId, optional)
        AddPbxFrameworkBuildPhase(app, name, firstId)
        AddPbxGroup(app, name, secondId)
    End

    Function AddFrameworkFromPath:Void(app:App, name:String, optional:Bool=False)
        If ContainsFramework(app, name) Then Return

        Local firstId:String = GenerateUniqueId(app)
        Local secondId:String = GenerateUniqueId(app)

        AddPbxFileReferencePath(app, name, secondId)
        EnsureSearchPathWithSRCROOT(app, "Debug")
        EnsureSearchPathWithSRCROOT(app, "Release")

        AddPbxBuildFile(app, name, firstId, secondId, optional)
        AddPbxFrameworkBuildPhase(app, name, firstId)
        AddPbxGroup(app, name, secondId)
    End

    Function GetProject:File(app:App)
        Return app.TargetFile("MonkeyGame.xcodeproj/project.pbxproj")
    End

    Function GetPlist:File(app:App)
        Return app.TargetFile("MonkeyGame-Info.plist")
    End

    Function ContainsFramework:Bool(app:App, name:String)
        Return GetProject(app).Contains("/* " + name + " ")
    End

    Private

    ' Copied from michaelcontento/bono
    ' See: https://github.com/michaelcontento/bono/blob/master/src/helper/mathhelper.monkey#L56-L67
    Const HEX_CHARS:String = "0123456789ABCDEF"
    Function IntToHex:String(value:Int)
        If value < 9 Then Return "" + value

        Local result:String

        While value > 0
            result = String.FromChar(HEX_CHARS[(value Mod 16)]) + result
            value /= 16
        End

        Return result
    End

    Function GenerateUniqueId:String(app:App)
        Local file:File = GetProject(app)
        Local result:String

        Repeat
            result = GenerateRandomId()
        Until Not file.Contains(result)

        Return result
    End

    Function GenerateRandomId:String()
        Local result:String = ""
        While result.Length() < 24
            result += IntToHex(Rnd(0, 17))
        End

        ' Ensure length of 24 even if the last random number was 10
        Return result[0..24]
    End

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

    Function AddPbxBuildFile:Void(app:App, name:String, firstId:String, secondId:string, optional:Bool)
        Local settings:String = ""
        If optional
            settings = "settings = {ATTRIBUTES = (Weak, ); };"
        End

        Local match:String = "/* End PBXBuildFile section"
        Local patchStr:String = "~t~t" +
            firstId + " " +
            "/* " + name + " in Frameworks */ = " +
            "{isa = PBXBuildFile; " +
            "fileRef = " + secondId + " /* " + name + "*/; " +
            settings + " };"
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
