Strict

Private

Import wizard.app
Import wizard.file
Import wizard.helperos

Public

Class Ios Abstract
    Global app:App

    Global defaultProjectFile$ = "MonkeyGame.xcodeproj/project.pbxproj"

    Function AddToPlist:Void(plistKey$, plistString$)
        Local plist:File = GetPlist()
        If (plist.Contains("<key>" + plistKey + "</key>")) Return

        plist.InsertBefore(
            "</dict>~n</plist>",
            "~t<key>" + plistKey + "</key>~n" +
            "~t<string>" + plistString + "</string>~n")
    End

    Function NSAllowsArbitraryLoads:Void()
        Local plist:File = GetPlist()

        If (plist.Contains("<key>NSAppTransportSecurity</key>") And plist.Contains("<key>NSAllowsArbitraryLoads</key>")) Return

        'Remove Old Values
        Local startLines := plist.FindLines("<key>NSAppTransportSecurity</key>")
        For Local l := startLines.Length-1 To 0 Step -1
            Local startLine := startLines[l]
            Local rows := 0
            Repeat
                rows += 1
            Until plist.GetLine(startLine + rows).Trim().StartsWith("</")

            If rows <= 0 Then Return

            For Local i := rows To 0 Step -1
                plist.RemoveLine(startLine + i)
            End
        Next

        plist.InsertBefore(
            "</dict>~n</plist>",
            "~t<key>NSAppTransportSecurity</key>~n" + 
            "~t<dict>~n" + 
            "~t~t<key>NSAllowsArbitraryLoads</key>~n" + 
            "~t~t<true/>~n" + 
            "~t</dict>")

        plist.Save()
    End

    Function RequiresFullscreen:Void()
        Local plist:File = GetPlist()

        If (plist.Contains("<key>UIRequiresFullScreen</key>"))
            Return
        End
        plist.InsertBefore(
            "</dict>~n</plist>",
            "~t<key>UIRequiresFullScreen</key>~n" + 
            "~t<true/>")

        plist.Save()
    End

    Function AddDylib:String(libFile$)
        If (GetProject().Contains("/* " + libFile + " */"))
            app.LogInfo("Dylib already added: " + libFile)
            Return ""
        End

        Local firstId:String = GenerateUniqueId()
        Local secondId:String = GenerateUniqueId()

        AddPbxFileReferenceFile(secondId, libFile, libFile, "~qcompiled.mach-o.dylib~q")
        AddPbxBuildFile(libFile, firstId, secondId, False)

        AddPbxFrameworkBuildPhase(libFile, firstId)
        AddPbxGroupChild("MonkeyGame", libFile, secondId, "name = MonkeyGame;")

        EnsureLibrarySearchPath("~q$(inherited)~q")
        EnsureLibrarySearchPath("~q$(PROJECT_DIR)~q")

        Return firstId
    End

    Function AddTextFile:String(file$)
        Local filePath := app.GetTargetDir() + "/xcode/" + file
        If (Not FileExists(filePath))
            app.LogInfo("File not found: " + file)
            Return ""            
        End

        If (GetProject().Contains("/* " + file + " */"))
            app.LogInfo("File already added: " + file)
            Return ""
        End

        Local firstId:String = GenerateUniqueId()
        Local secondId:String = GenerateUniqueId()

        AddPbxFileReferenceFile(secondId, file, file, "text")
        AddPbxBuildFile(file, firstId, secondId, False)

        AddPbxGroupChild("MonkeyGame", file, secondId, "name = MonkeyGame;")

        Return firstId
    End

    Function AddCopyBuildPhase:Void(files:String[][])
        Local sectionId:String = GenerateUniqueId()

        Local project := GetProject()

        If (project.Contains("Begin PBXCopyFilesBuildPhase section"))
            app.LogInfo("Warning: Cannot add Copy Build Phase (already present in XCode Project)")
            Return
        End

        Local patch := "~n/* Begin PBXCopyFilesBuildPhase section */~n"
        patch += "~t~t" + sectionId + " /* CopyFiles */ = {~n"
        patch += "~t~t~tisa = PBXCopyFilesBuildPhase;~n"
        patch += "~t~t~tbuildActionMask = 2147483647;~n"
        patch += "~t~t~tdstPath = ~q~q;~n"
        patch += "~t~t~tdstSubfolderSpec = 6;~n"
        patch += "~t~t~tfiles = (~n"

        For Local file := EachIn files
            Local fileName$ = file[0]
            Local fileId$ = file[1]
            patch += "~t~t~t~t" + fileId + " /* " + fileName + " in CopyFiles */,~n"
        Next

        patch += "~t~t~t);~n"
        patch += "~t~t~trunOnlyForDeploymentPostprocessing = 0;~n"
        patch += "~t~t};~n"
        patch += "/* End PBXCopyFilesBuildPhase section */"

        project.InsertAfter("/* End PBXBuildFile section */", patch)
        project.Save()

        AddPbxNativeTargetBuildPhase("Copy Files", sectionId)
    End
       
    Function AddPbxNativeTargetBuildPhase:Void(name:String, id:String)
        Local patchStr:String = "~t~t~t~t" + id + " /* " + name + " */,"
        Local searchBegin:String = "Begin PBXNativeTarget section"
        Local searchEnd:String = "End PBXNativeTarget section"
        Local addAfter:String = "buildPhases = ("
        Local target:File = GetProject()

        If target.ContainsBetween(patchStr, searchBegin, searchEnd) Then Return

        If Not target.ContainsBetween(addAfter, searchBegin, searchEnd)
            app.LogWarning("Unable To add " + name + " PBXFrameworksBuildPhase")
            app.LogWarning("Please add this framework manually!")
        Else
            target.InsertAfterBetween(addAfter, patchStr, searchBegin, searchEnd)
        End
        target.Save()
    End

 
    Function AddFramework:Void(name:String, optional:Bool=False)
        app.LogInfo("Adding framework: " + name)
        If ContainsFramework(name) Then Return

        Local firstId:String = GenerateUniqueId()
        Local secondId:String = GenerateUniqueId()

        AddPbxFileReferenceSdk(name, secondId)

        AddPbxBuildFile(name, firstId, secondId, optional)
        AddPbxFrameworkBuildPhase(name, firstId)
        AddPbxGroupChild("Frameworks", name, secondId)
    End

    Function EnsureHeaderSearchPath:Void(name$)
        Local file := GetProject()
        Local lines := file.FindLines("HEADER_SEARCH_PATHS =")
        For Local lineNo := lines.Length-1 To 0 Step -1 
            Local line := file.GetLine(lines[lineNo])
            If (Not line.Contains("("))
                line = line.Replace(";", ",")
                file.ReplaceLine(lines[lineNo], line.Replace(" = ", " = (~n~t~t~t~t~t") + "~n~t~t~t~t);")
            End
        Next

        lines = file.FindLines("HEADER_SEARCH_PATHS = (")
        For Local lineNo := lines.Length-1 To 0 Step -1
            Local line := file.GetLine(lines[lineNo])
            line += "~n" + "~t~t~t~t~t" + name + ","


            Local followLine := 1
            Local libFound := False
            Repeat
                Local nextLine := file.GetLine(lines[lineNo]+followLine)
                If (nextLine = "~t~t~t~t);") Then Exit
                If (nextLine.Contains(name)) Then libFound = True ; Exit
                followLine += 1
            Forever

            If (Not libFound) Then file.ReplaceLine(lines[lineNo], line)
        Next

        file.Save()
    End

    Function EnsureLibrarySearchPath:Void(name$)
        Local file := GetProject()

        If (Not file.Contains("LIBRARY_SEARCH_PATHS ="))
            file.InsertAfter("~t~t~tbuildSettings = {", "~t~t~t~tLIBRARY_SEARCH_PATHS = (~n~t~t~t~t);")
        End

        Local lines := file.FindLines("LIBRARY_SEARCH_PATHS =")        
        For Local lineNo := lines.Length-1 To 0 Step -1 
            Local line := file.GetLine(lines[lineNo])
            If (Not line.Contains("("))
                line = line.Replace(";", ",")
                file.ReplaceLine(lines[lineNo], line.Replace(" = ", " = (~n~t~t~t~t~t") + "~n~t~t~t~t);")
            End
        Next

        lines = file.FindLines("LIBRARY_SEARCH_PATHS = (")
        For Local lineNo := lines.Length-1 To 0 Step -1
            Local line := file.GetLine(lines[lineNo])
            line += "~n" + "~t~t~t~t~t" + name + ","


            Local followLine := 1
            Local libFound := False
            Repeat
                Local nextLine := file.GetLine(lines[lineNo]+followLine)
                If (nextLine = "~t~t~t~t);") Then Exit
                If (nextLine.Contains(name)) Then libFound = True ; Exit
                followLine += 1
            Forever

            If (Not libFound) Then file.ReplaceLine(lines[lineNo], line)
        Next

        file.Save()
    End

    Function EnsureOtherLDFlags:Void(name$)
        Local file := GetProject()
        Local lines := file.FindLines("OTHER_LDFLAGS =")
        For Local lineNo := lines.Length-1 To 0 Step -1 
            Local line := file.GetLine(lines[lineNo])
            If (Not line.Contains("("))
                line = line.Replace(";", ",")
                file.ReplaceLine(lines[lineNo], line.Replace(" = ", " = (~n~t~t~t~t~t") + "~n~t~t~t~t);")
            End
        Next

        lines = file.FindLines("OTHER_LDFLAGS = (")
        For Local lineNo := lines.Length-1 To 0 Step -1
            Local line := file.GetLine(lines[lineNo])
            line += "~n" + "~t~t~t~t~t" + name + ","


            Local followLine := 1
            Local libFound := False
            Repeat
                Local nextLine := file.GetLine(lines[lineNo]+followLine)
                If (nextLine = "~t~t~t~t);") Then Exit
                If (nextLine.Contains(name)) Then libFound = True ; Exit
                followLine += 1
            Forever

            If (Not libFound) Then file.ReplaceLine(lines[lineNo], line)
        Next

        file.Save()
    End
    Function AddFrameworkFromPath:Void(name:String, optional:Bool=False)
        app.LogInfo("Adding framework from path: " + name)
        If ContainsFramework(name) Then Return

        Local firstId:String = GenerateUniqueId()
        Local secondId:String = GenerateUniqueId()

        AddPbxFileReferencePath(name, secondId)

        EnsureSearchPathWithSRCROOT("Debug")
        EnsureSearchPathWithSRCROOT("Release")

        AddPbxBuildFile(name, firstId, secondId, optional)

        AddPbxFrameworkBuildPhase(name, firstId)
        AddPbxGroupChild("Frameworks", name, secondId)
    End

    Function GetProject:File()
        Return app.TargetFile(defaultProjectFile)
    End

    Function GetPlist:File()
        Return app.TargetFile("MonkeyGame-Info.plist")
    End

    Function GetMainSource:File()
        Return app.TargetFile("main.mm")
    End

    Function ContainsFramework:Bool(name:String)
        Return GetProject().Contains("/* " + name + " ")
    End

    Function UpdateProjectSetting:Void(key:String, newValue:String)
        Local oldValue := GetProjectSetting(key)
        Local oldRow := key + " = " + oldValue + ";"
        Local oldRowQuoted := key + " = ~q" + oldValue + "~q;"

        Local file := GetProject()

        For Local old := EachIn [oldRow, oldRowQuoted]
            If Not file.Contains(old) Then Continue
            file.Replace(old, key + " = ~q" + newValue + "~q;")
        End
    End

    Function UpdateDeploymentTarget:Void(newValue:String)
        Local file := GetProject()
        Local lines := file.FindLines("IPHONEOS_DEPLOYMENT_TARGET" + " = ")

        For Local l := EachIn lines
            file.ReplaceLine(l, "~t~t~t~t" + "IPHONEOS_DEPLOYMENT_TARGET" + " = " + newValue + ";")
        Next
    End

    Function GetProjectSetting:String(key:String)
        Local file := GetProject()
        Local lines := file.FindLines(key + " = ")

        If lines.Length() = 0
            app.LogError("No current " + key + " found")
        End

        Local lastValue := ""
        For Local i := 0 Until lines.Length()
            Local currValue := ExtractSettingKey(file.GetLine(lines[i]))

            If lastValue And lastValue <> currValue
                app.LogError("Different old " + key + " settings found")
            End

            lastValue = currValue
        End

        If lastValue.Length() <= 0
            app.LogError("No old " + key + " found")
        End

        Return lastValue
    End

    Function UpdatePlistSetting:Void(key:String, value:String)
        Local plist:File = GetPlist()

        Local keyLine:Int = GetKeyLine(plist, key)
        Local valueLine:Int = keyLine + 1
        ValidateValueLine(plist, valueLine)

        Local newVersion:String = "~t<string>" + value + "</string>"
        plist.ReplaceLine(valueLine, newVersion)
    End

    Function GenerateUniqueId:String()
        Local file:File = GetProject()
        Local result:String

        Repeat
            result = GenerateRandomId()
        Until Not file.Contains(result)

        Return result
    End

    Function EnsurePBXVariantGroupSection:Void()
        If GetProject().Contains("PBXVariantGroup section") Then Return

        Local match:String = "/* End PBXSourcesBuildPhase section */"
        Local text:String = "~n" +
            "/* Begin PBXVariantGroup section */~n" +
            "/* End PBXVariantGroup section */"
        GetProject().InsertAfter(match, text)
    End

    Function AddPBXVariantGroup:Void(id:String, name:String, ids:String[], children:String[])
        EnsurePBXVariantGroupSection()

        Local childrenRecords:String = ""
        For Local i:Int = 0 Until children.Length()
            childrenRecords += "~t~t~t~t" +
                ids[i] + " /* " + children[i] + " */,~n"
        End

        Local match:String = "/* End PBXVariantGroup section */"
        Local text:String = "" +
            "~t~t" + id + " /* " + name + " */ = {~n" +
            "~t~t~tisa = PBXVariantGroup;~n" +
            "~t~t~tchildren = (~n" +
            childrenRecords +
            "~t~t~t);~n" +
            "~t~t~tname = " + name + ";~n" +
            "~t~t~tsourceTree = ~q<group>~q;~n" +
            "~t~t};"
        GetProject().InsertBefore(match, text)
    End

    Function AddPBXVariantGroupChildren:Void(name$, childId$, childName$)
        Local variantGroup := GetProject().GetContentBetween("/* Begin PBXVariantGroup section */", "/* End PBXVariantGroup section */")
        Local pos := variantGroup.Find(" /* " + name + " */")
        If (pos = -1) 
            Print name + " not found, Creating new group"
            AddPBXVariantGroup(Ios.GenerateUniqueId(), name, [childId], [childName])            
            Return
        End

        Local posChild := variantGroup.Find("children = (", pos)
        If (posChild = -1) Then app.LogError("Child in group " + name + " not found!") ; Return

        Local newChild := "~t~t~t~t" + childId + " /* " + childName + " */,~n"
        Local newContent := variantGroup[0..(posChild + 12)] + newChild + variantGroup[(posChild + 12)..variantGroup.Length()]

        GetProject().Replace(variantGroup, newContent)
    End

    Function AddKnownRegion:Void(region:String)
        Local text:String = "~t~t~t~t~q" + region + "~q,"
        GetProject().InsertAfter("knownRegions = (", text)
    End

    Function EnsureSearchPathWithSRCROOT:Void(config:String)
        Local searchStr:String = "FRAMEWORK_SEARCH_PATHS"
  ''      Local searchBegin:String = "" +
 ''           "/* " + config + " */ = {~n" +
''            "~t~t~tisa = XCBuildConfiguration;"

        Local searchBegin:String = "" +
            "/* " + config + " */ = {"

        Local searchEnd:String = "name = " + config + ";"
        Local target:File = GetProject()

        ' Whole setting is missing
        If Not target.ContainsBetween(searchStr, searchBegin, searchEnd)

            If (Not target.Contains(searchBegin)) Then Error searchBegin + " --- Not found! in " + target.Get()
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

    Function SetBitcode:Void(str$ = "NO")
        Local project := GetProject()
        If (Not project.Contains("ENABLE_BITCODE = "))
            project.InsertAfter("buildSettings = {", "~t~t~t~tENABLE_BITCODE = " + str + ";")
        End

        Local lines := project.FindLines("ENABLE_BITCODE = ");
        For Local line := 0 To lines.Length-1
            project.ReplaceLine(lines[line], "~t~t~t~tENABLE_BITCODE = " + str + ";")
        Next

        project.Save()
    End

    Function AddPbxGroupChild:Void(where:String, name:String, id:String, searchEnd$ = "End PBXGroup section")
        Local patchStr:String = "~t~t~t~t" + id + " /* " + name + " */,"
        Local searchBegin:String = " /* " + where + " */ = {"
        Local addAfter:String = "children = ("
        Local target:File = GetProject()

        If target.ContainsBetween(patchStr, searchBegin, searchEnd) Then Return

        If Not target.ContainsBetween(addAfter, searchBegin, searchEnd)
            app.LogWarning("Unable to add " + name + " to PBXGroup " + where)
            app.LogWarning("Please add this framework manually!")
        Else
            target.InsertAfterBetween(addAfter, patchStr, searchBegin, searchEnd)
        End
    End


    Function AddPbxGroup:Void(name:String, id:String, path$)
        Local headline:String = id + " /* " + name + " */ = {"
        If GetProject().Contains(headline) Then Return
        Print name

        Local text:String = "~t~t" + headline + "~n" +
            "~t~t~tisa = PBXGroup;~n" +
            "~t~t~tchildren = (~n" +
            "~t~t~t);~n" +
            "~t~t~tpath = " + path + ";~n" +
            "~t~t~tsourceTree = ~q<group>~q;~n" +
            "~t~t};"

        GetProject().InsertBefore("/* End PBXGroup section */", text)
    End

    Function RegisterPxbGroup:Void(name:String, id:String)
        Local lines:Int[] = GetProject().FindLines("/* CustomTemplate */ = {")
        If lines.Length() <> 1
            app.LogError("Unable to locate CustomTemplate definition")
        End

        Local childLine:Int = lines[0] + 2
        If Not GetProject().GetLine(childLine).Contains("children = (")
            app.LogError("Unable to locate children of CustomTemplate")
        End

        Local text:String = "~t~t~t~t" + id + " /* " + name + " */,"
        GetProject().InsertAfterLine(childLine, text)
    End

    Function AddPbxFrameworkBuildPhase:Void(name:String, id:String)
        Local patchStr:String = "~t~t~t~t" + id + " /* " + name + " in Frameworks */,"
        Local searchBegin:String = "Begin PBXFrameworksBuildPhase section"
        Local searchEnd:String = "End PBXFrameworksBuildPhase section"
        Local addAfter:String = "files = ("
        Local target:File = GetProject()

        If target.ContainsBetween(patchStr, searchBegin, searchEnd) Then Return

        If Not target.ContainsBetween(addAfter, searchBegin, searchEnd)
            app.LogWarning("Unable To add " + name + " PBXFrameworksBuildPhase")
            app.LogWarning("Please add this framework manually!")
        Else
            target.InsertAfterBetween(addAfter, patchStr, searchBegin, searchEnd)
        End
    End

    Function AddPbxFileReferenceSdk:Void(name:String, id:String)
        Local patchStr:String = "~t~t" +
            id + " " +
            "/* " + name + " */ = " +
            "{isa = PBXFileReference; " +
            "lastKnownFileType = wrapper.framework; " +
            "name = " + name + "; " +
            "path = System/Library/Frameworks/" + name + "; " +
            "sourceTree = SDKROOT; };"

        AddPbxFileReference(name, patchStr)
    End

    Function AddPbxFileReferencePath:Void(name:String, id:String)
        Local patchStr:String = "~t~t" +
            id + " " +
            "/* " + name + " */ = " +
            "{isa = PBXFileReference; " +
            "lastKnownFileType = wrapper.framework; " +
            "path = " + name + "; " +
            "sourceTree = ~q<group>~q; };"

        AddPbxFileReference(name, patchStr)
    End

    Function AddIconPBXResourcesBuildPhase:Void(filename:String, id:String)
        Local project := GetProject()
        Local lines := project.FindLines("isa = PBXResourcesBuildPhase;")
        Local insertLine := lines[0] + 2

        If Not project.GetLine(insertLine).Contains("files = (")
            app.LogError("Unable to add icon into PBXResourcesBuildPhase")
        End

        project.InsertAfterLine(
            insertLine,
            "~t~t~t~t" + id + " " +
            "/* " + filename + " in Resources */,")
    End

    Function AddIconPBXGroup:Void(filename:String, id:String)
        Local project := GetProject()
        Local lines := project.FindLines("29B97314FDCFA39411CA2CEA /* CustomTemplate */")
        Local insertLine := lines[0] + 2

        If Not project.GetLine(insertLine).Contains("children = (")
            app.LogError("Unable to add icon into PBXGroup")
        End

        project.InsertAfterLine(
            insertLine,
            "~t~t~t~t" + id + " " +
            "/* " + filename + " */,")
    End

    Function AddIconPBXFileReference:Void(filename:String, id:String)
        GetProject().InsertBefore(
            "/* End PBXFileReference section */",
            "~t~t" + id + " " +
            "/* " + filename + " */ = " +
            "{isa = PBXFileReference; lastKnownFileType = image.png; " +
            "path = ~q" + filename + "~q; sourceTree = ~q<group>~q; };")
    End

    Function AddIconPBXBuildFile:Void(filename:String, firstId:String, secondId:String)
        GetProject().InsertBefore(
            "/* End PBXBuildFile section */",
            "~t~t" + firstId + " " +
            "/* " + filename + " in Resources */ = " +
            "{isa = PBXBuildFile; fileRef = " + secondId + " " +
            "/* " + filename + " */; };")
    End

    Function AddPbxFileReferenceFile:Void(id:String, name:String, path:String, type:String)
        Local patchStr:String = "~t~t" +
            id + " " +
            "/* " + name + " */ = " +
            "{isa = PBXFileReference; " +
            "fileEncoding = 4; " +
            "lastKnownFileType = " + type + "; " +
            "path = " + path + "; " +
            "sourceTree = ~q<group>~q; };"

        AddPbxFileReference(name, patchStr)
    End

    Function AddPbxFileReferenceLProj:Void(id:String, name:String, path:String)
        Local patchStr:String = "~t~t" +
            id + " " +
            "/* " + name + " */ = " +
            "{isa = PBXFileReference; " +
            "lastKnownFileType = text.plist.strings; " +
            "name = ~q" + name + "~q; " +
            "path = ~q" + name + ".lproj/" + path + "~q; " +
            "sourceTree = ~q<group>~q; };"

        AddPbxFileReference(name, patchStr)
    End

    Function AddPbxFileReference:Void(name:String, patchStr:String)
        Local match:String = "/* End PBXFileReference section"
        Local target:File = GetProject()

        If target.Contains(patchStr) Then Return

        If Not target.Contains(match)
            app.LogWarning("Unable to add " + name + " PBXFileReference")
            app.LogWarning("Please add this framework manually!")
        Else
            target.InsertBefore(match, patchStr)
        End
    End

    Function AddPbxResource:Void(name:String, id:String)
        Local lines:Int[] = GetProject().FindLines(" /* Resources */ = {")
        If lines.Length() <> 2
            app.LogError("Unable to detect PBXResourcesBuildPhase section")
        End

        Local filesLine:Int = lines[1] + 3
        If Not GetProject().GetLine(filesLine).Contains("files = (")
            app.LogError("Unable to find files in PBXResourcesBuildPhase")
        End

        Local text:String = "~t~t~t~t" + id + " /* " + name + " in Resources */,"
        GetProject().InsertAfterLine(filesLine, text)
    End

    Function AddPbxBuildFile:Void(name:String, firstId:String, secondId:String, optional:Bool, compilerFlags:String="")
        Local settings:String = "settings = {};"
        If optional
            settings = settings.Replace("};", "ATTRIBUTES = (Weak, ); };")
        End
        If compilerFlags
            settings = settings.Replace("};", "COMPILER_FLAGS = ~q" + compilerFlags + "~q; };")
        End

        Local match:String = "/* End PBXBuildFile section"
        Local patchStr:String = "~t~t" +
            firstId + " " +
            "/* " + name + " in Frameworks */ = " +
            "{isa = PBXBuildFile; " +
            "fileRef = " + secondId + " /* " + name + " */; " +
            settings + " };"
        Local target:File = GetProject()

        If target.Contains(patchStr) Then Return

        If Not target.Contains(match)
            app.LogWarning("Unable to add " + name + " PBXBuildFile")
            app.LogWarning("Please add this framework manually!")
        Else
            target.InsertBefore(match, patchStr)
        End
    End

    Private

    Function ExtractSettingKey:String(row:String)
        Local parts := row.Split(" ")
        Local key := parts[parts.Length() - 1]
        Return key.Replace(";", "").Trim()
    End


    Function GetKeyLine:Int(plist:File, key:String)
        Local lines:Int[] = plist.FindLines("<key>" + key + "</key>")

        If lines.Length() <> 1
            app.LogError("Found zero Or more than one setting For " + key)
        End

        Return lines[0]
    End

    Function ValidateValueLine:Void(plist:File, line:Int)
        Local lineStr:String = plist.GetLine(line)
        Local hasStrStart:Bool = lineStr.Contains("<string>")
        Local hasStrEnd:Bool = lineStr.Contains("</string>")

        If hasStrStart And hasStrEnd Then Return

        app.LogError("Expected <string></string> after <key>CFBundleVersion</key>")
    End

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

    Function GenerateRandomId:String()
        Local result:String = ""
        While result.Length() < 24
            result += IntToHex(Rnd(0, 17))
        End

        ' Ensure length of 24 even if the last random number was 10
        Return result[0..24]
    End
End
