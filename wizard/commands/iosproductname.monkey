Strict

Private

Import wizard.app
Import wizard.command
Import wizard.ios

Public

Class IosProductName Implements Command
    Private

    Field app:App

    Public

    Method Run:Void(app:App)
        Self.app = app

        Local newName := GetNewName()
        Local newNameTrimmed := newName.Replace(" ", "").Trim()
        Local oldName := Ios.GetProjectSetting("PRODUCT_NAME")

        Local file := Ios.GetProject()
        file.Replace(
            "/* " + oldName + ".app */",
            "/* " + newNameTrimmed + ".app */")
        file.Replace(
            "; path = " + oldName + ".app;",
            "; path = " + newNameTrimmed + ".app;")
        Ios.UpdateProjectSetting("PRODUCT_NAME", newName)
    End

    Private

    Method GetNewName:String()
        If app.GetAdditionArguments().Length() <> 1
            app.LogError("Product name argument missing")
        End

        Return app.GetAdditionArguments()[0]
    End
End
