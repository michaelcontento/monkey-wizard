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
        Local oldName := GetOldName()

        Local file := Ios.GetProject()
        file.Replace(
            "/* " + oldName + ".app */",
            "/* " + newName + ".app */")
        file.Replace(
            "; path = " + oldName + ".app;",
            "; path = " + newName + ".app;")
        file.Replace(
            "PRODUCT_NAME = " + oldName + ";",
            "PRODUCT_NAME = " + newName + ";")
    End

    Private

    Method GetOldName:String()
        Local file := Ios.GetProject()
        Local lines := file.FindLines("PRODUCT_NAME = ")

        If lines.Length() = 0
            app.LogError("No current PRODUCT_NAME found")
        End

        Local lastName := ""
        For Local i := 0 Until lines.Length()
            Local currName := ExtractName(file.GetLine(lines[i]))

            If lastName And lastName <> currName
                app.LogError("Different old PRODUCT_NAME settings found")
            End

            lastName = currName
        End

        If lastName.Length() <= 0
            app.LogError("No old PRODUCT_NAME found")
        End

        Return lastName
    End

    Method ExtractName:String(row:String)
        Local parts := row.Split(" ")
        Local name := parts[parts.Length() - 1]
        Return name.Replace(";", "").Trim()
    End

    Method GetNewName:String()
        If app.GetAdditionArguments().Length() <> 1
            app.LogError("Product name argument missing")
        End

        Return app.GetAdditionArguments()[0]
    End
End
