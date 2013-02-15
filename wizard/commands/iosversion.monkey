Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios

Public

Class IosVersion Implements Command
    Method Run:Void(app:App)
        Local plist:File = Ios.GetPlist(app)

        Local keyLine:Int = GetKeyLine(app, plist)
        Local valueLine:Int = keyLine + 1
        ValidateValueLine(app, plist, valueLine)

        Local newVersion:String = "~t<string>" + GetVersion(app) + "</string>"
        plist.ReplaceLine(valueLine, newVersion)
    End

    Private

    Function GetKeyLine:Int(app:App, plist:File)
        Local lines:Int[] = plist.FindLines("<key>CFBundleVersion</key>")

        If lines.Length() <> 1
            app.LogError("Project contains zero or more than one CFBundleVersion")
        End

        Return lines[0]
    End

    Function ValidateValueLine:Void(app:App, plist:File, line:Int)
        Local lineStr:String = plist.GetLine(line)
        Local hasStrStart:Bool = lineStr.Contains("<string>")
        Local hasStrEnd:Bool = lineStr.Contains("</string>")

        If hasStrStart And hasStrEnd Then Return

        app.LogError("Expected <string></string> after <key>CFBundleVersion</key>")
    End

    Function GetVersion:String(app:App)
        If app.GetAdditionArguments().Length() <> 1
            app.LogError("Version string argument missing")
        End

        Return app.GetAdditionArguments()[0]
    End
End
