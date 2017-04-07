Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios
Import os
Import wizard.cocoapods

Public

Class IosAllowUnsecureConnections Implements Command
    Method Run:Void(app:App)
        Ios.NSAllowsArbitraryLoads()
    End
   
End
