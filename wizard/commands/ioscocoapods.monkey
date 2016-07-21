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

Class IosCocoapods Implements Command
    Method Run:Void(app:App)
        CocoaPods.Init(app)        
        CocoaPods.Install(app)        

        app.LogInfo("Cocoapods installed!")
    End
   
End
