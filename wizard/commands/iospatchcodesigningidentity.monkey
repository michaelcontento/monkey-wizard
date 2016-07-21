Strict

Private

Import wizard.app
Import wizard.command
Import wizard.ios

#Rem
    This command replaces the Release Code Signing Identity
    to the Default Distribution Identity.

    The parsing is a little bit hacky (simple find, move around, replace)
    but it works with the current version
    of the monkey xcode project and the sdk and helps to keep
    the code short & simple (instead of a real parsing of the xcode porject file).
#End

Public

Class IosPatchCodeSigningIdentity Implements Command
    Method Run:Void(app:App)
        Local file := Ios.GetProject()

        Local line := file.FindLines("~qCODE_SIGN_IDENTITY[sdk=iphoneos*]~q = ~qiPhone Developer~q;")

        Local newLineContent := file.GetLine(line[1]).Replace("iPhone Developer", "iPhone Distribution")
        file.ReplaceLine(line[1], newLineContent)

        Local lines := file.FindLines("name = Release")
        Local appendAfterLine := lines[0] - 2
        file.InsertAfterLine(appendAfterLine, newLineContent.Replace("[sdk=iphoneos*]", ""))

        file.Save()
    End
End
