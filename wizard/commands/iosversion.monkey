Strict

Private

Import wizard.app
Import wizard.command
Import wizard.ios

Public

Class IosVersion Implements Command
    Method Run:Void(app:App)
        Ios.UpdatePlistSetting("CFBundleVersion", GetVersion(app))

        Local plist := Ios.GetPlist()

        plist.InsertBefore(
            "</dict>~n</plist>",
            "~t<key>CFBundleShortVersionString</key>~n" + 
            "~t<string>" + GetVersion(app) + "</string>")

        plist.Save()
    End

    Private

    Function GetVersion:String(app:App)
        If app.GetAdditionArguments().Length() <> 1
            app.LogError("Version string argument missing")
        End

        Return app.GetAdditionArguments()[0]
    End
End
